#include "RecorderServer.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <SD.h>
#include "freertos/semphr.h"
#include "../pages/_widgets/StatusBar.h"

// Shared SPI-bus mutex (created in m5gfx_lvgl_init). The CoreS3 LCD and SD card
// share the SPI bus, so every SD op (writer task, upload reads here) and every
// LCD flush must be serialised through this one mutex.
extern SemaphoreHandle_t g_spi_bus_mutex;

// WiFi credentials + Headspace upload URL are optional at build time: the public
// repo compiles without include/secrets.h (recorder just runs offline). Provide
// it to enable WiFi serving and Headspace uploads.
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif
#ifndef WIFI_SSID
#  define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#  define WIFI_PASS ""
#endif
#ifndef REC_HOSTNAME
#  define REC_HOSTNAME "core-s3"
#endif
#ifndef HEADSPACE_UPLOAD_URL
#  define HEADSPACE_UPLOAD_URL ""  // empty -> compile-time fallback disabled
#endif

// Filenames already persisted by Headspace, one per line. Lets the device avoid
// re-uploading the same recording across reboots.
#define UPLOADED_MARKER "/UPLOADED.txt"
#define UPLOAD_RETRY_MS 30000UL  // back-off between upload cycles while anything's un-acked

namespace Net {

RecorderServer Server;

static bool creds_present() { return strlen(WIFI_SSID) > 0; }

static void bus_take() { if (g_spi_bus_mutex) xSemaphoreTake(g_spi_bus_mutex, portMAX_DELAY); }
static void bus_give() { if (g_spi_bus_mutex) xSemaphoreGive(g_spi_bus_mutex); }

void RecorderServer::begin() {
    if (!creds_present()) {
        Serial.println("[NET] No WiFi creds (include/secrets.h) — server disabled");
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(REC_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[NET] Connecting to \"%s\"...\n", WIFI_SSID);
}

void RecorderServer::loop() {
    if (!creds_present()) return;

    // Suspend all WiFi/HTTP work while recording so it can't compete with the
    // I2S real-time capture loop. WiFi.reconnect() in particular blocks the
    // single Arduino task for seconds at a time — during which M5.Mic.record()
    // isn't drained and the I2S DMA overflows. That was the most likely cause
    // of the ~6-min silent-mic failure at off-home meetings (constant reconnect
    // attempts -> DMA starvation -> codec fault). On-home/bench WiFi stays
    // connected the whole time so there are no reconnects, hence the bench
    // test stayed clean for 9 min. Server resumes the instant recording stops
    // (state set by AppRecorder via setRecording()).
    if (recording) {
        if (!was_paused) {
            Serial.println("[NET] paused — WIFI_OFF (yielding radio to I2S capture)");
            // Pausing only our loop wasn't enough: the underlying IDF WiFi task
            // keeps retrying association in the background when the SSID isn't
            // reachable (at meetings, off-home WiFi). That competes with the
            // I2S DMA and crashes the device after ~13 min. Fully OFF the radio
            // so no background WiFi work can run at all.
            WiFi.mode(WIFI_OFF);
            started     = false;  // mDNS + HTTP need to re-init when WiFi comes back
            was_paused  = true;
        }
        return;
    }
    if (was_paused) {
        Serial.println("[NET] resumed — re-enabling WiFi");
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(REC_HOSTNAME);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        was_paused = false;
        // started is false; the connected-and-not-started branch below will
        // re-init mDNS + HTTP once association completes.
    }

    // Maintain the un-acked recording count for the status-bar counter. Recompute
    // only when flagged dirty (boot, and after each recording stops) — never on a
    // timer — so the UI thread never scans the card. We're idle here (recording
    // returns early above), so the SD bus is free; this also keeps the offline
    // backlog count fresh even before WiFi is up.
    if (pending_dirty_) {
        recomputePending();
        pending_dirty_ = false;
    }

    bool connected = (WiFi.status() == WL_CONNECTED);

    // Bring up mDNS + HTTP once, the first time WiFi comes up. mDNS is torn down
    // when WiFi goes OFF during recording (started reset to false above), so this
    // re-runs the service browse on every recording->idle transition, not just at
    // boot — the server's DHCP IP may have changed.
    if (connected && !started) {
        if (MDNS.begin(REC_HOSTNAME)) {
            MDNS.addService("http", "tcp", 80);
            MDNS.addServiceTxt("http", "tcp", "path", "/api/recordings");
        }
        resolveUploadUrl();      // browse for _otl-recordings._tcp (or fallback)
        routes();
        server.begin();
        started        = true;
        upload_pending = true;   // push anything un-acked as soon as we're up
        Serial.printf("[NET] Up: http://%s.local  (%s)\n", REC_HOSTNAME,
                      WiFi.localIP().toString().c_str());
    }

    if (started) server.handleClient();

    // Push recordings to Headspace when idle — never mid-recording (WiFi is OFF
    // then anyway). One file per loop pass keeps handleClient() responsive.
    // upload_pending forces an immediate cycle (post-record, resync); otherwise
    // the periodic retry only fires when there's actually a backlog to clear, so
    // an all-sent device never re-scans the card on the 30s tick.
    if (started && !recording &&
        (upload_pending ||
         (pending_count_ > 0 && millis() - last_upload > UPLOAD_RETRY_MS))) {
        uploadCycle();
    }

    if (!connected) {
        uint32_t now = millis();
        if (now - last_retry > 15000) {
            last_retry = now;
            WiFi.reconnect();
        }
    }
}

bool RecorderServer::wifiConnected() { return WiFi.status() == WL_CONNECTED; }

String RecorderServer::hostUrl() { return String("http://") + REC_HOSTNAME + ".local"; }

void RecorderServer::routes() {
    server.on("/", HTTP_GET, [this]() { handleRoot(); });
    server.on("/api/recordings", HTTP_GET, [this]() { handleList(); });
    // Operational: clear the sent-tracking marker and force a re-push of every
    // recording on the next idle pass. Lets the upload path be re-driven without
    // a physical re-record (USB serial control is unavailable — Serial is on UART0
    // since ARDUINO_USB_CDC_ON_BOOT was dropped). Refused while recording.
    server.on("/api/resync", HTTP_POST, [this]() {
        if (recording) {
            server.send(503, "application/json", "{\"error\":\"recording\"}");
            return;
        }
        bool removed   = SD.remove(UPLOADED_MARKER);
        upload_pending = true;
        pending_dirty_ = true;  // every recording is un-acked again — recount
        burst_total_   = 0;     // start a fresh burst on the next cycle
        burst_sent_    = 0;
        Serial.printf("[NET] resync: ack marker %s — re-pushing all recordings\n",
                      removed ? "cleared" : "absent");
        server.send(200, "application/json",
                    String("{\"status\":\"resync\",\"ack_cleared\":") +
                        (removed ? "true" : "false") + "}");
    });
    server.onNotFound([this]() { handleDownload(); });  // /rec/<name>
}

// Return just the basename of an SD path ("/REC_001.wav" -> "REC_001.wav").
static String basename_of(const String& path) {
    int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.substring(slash + 1) : path;
}

static bool is_recording_file(const String& base) {
    return base.startsWith("REC_") && base.endsWith(".wav");
}

void RecorderServer::handleList() {
    if (recording) {
        server.send(503, "application/json", "{\"error\":\"recording\"}");
        return;
    }
    String json = "[";
    File dir = SD.open("/");
    if (dir) {
        bool first = true;
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            String base = basename_of(String(f.name()));
            if (!f.isDirectory() && is_recording_file(base)) {
                if (!first) json += ",";
                first = false;
                json += "{\"name\":\"" + base + "\",\"size\":" +
                        String((uint32_t)f.size()) + "}";
            }
            f.close();
        }
        dir.close();
    }
    json += "]";
    server.send(200, "application/json", json);
}

void RecorderServer::handleDownload() {
    if (recording) {
        server.send(503, "text/plain", "recording");
        return;
    }
    String uri = server.uri();  // expect /rec/REC_NNN.wav
    if (!uri.startsWith("/rec/")) {
        server.send(404, "text/plain", "not found");
        return;
    }
    String name = uri.substring(5);
    if (name.indexOf('/') >= 0 || !is_recording_file(name)) {
        server.send(400, "text/plain", "bad name");
        return;
    }
    String path = "/" + name;
    if (!SD.exists(path)) {
        server.send(404, "text/plain", "not found");
        return;
    }
    File f = SD.open(path, FILE_READ);
    if (!f) {
        server.send(500, "text/plain", "open failed");
        return;
    }
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    size_t sent = server.streamFile(f, "audio/wav");
    f.close();
    // Diagnostics-only pull now: ack happens in the upload response handler
    // (push model), never on a served pull.
    Serial.printf("[NET] served %s (%u bytes)\n", name.c_str(), (uint32_t)sent);
}

void RecorderServer::handleRoot() {
    server.send(200, "text/plain",
                "otageLabs CoreS3 meeting recorder\n"
                "GET /api/recordings   list recordings (JSON)\n"
                "GET /rec/<name>       download a WAV\n");
}

bool RecorderServer::fileAcked(const String& name) {
    File f = SD.open(UPLOADED_MARKER, FILE_READ);
    if (!f) return false;
    bool found = false;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line == name) {
            found = true;
            break;
        }
    }
    f.close();
    return found;
}

void RecorderServer::ackFile(const String& name) {
    File f = SD.open(UPLOADED_MARKER, FILE_APPEND);
    if (!f) return;
    f.println(name);
    f.close();
}

// Discover the Headspace upload endpoint. mDNS service browse takes priority;
// the compile-time HEADSPACE_UPLOAD_URL is the fallback when the browse finds
// nothing. If neither resolves, upload_url stays empty and uploads are disabled.
// NOTE: queryService takes the bare service/proto — the lib prepends the '_'.
// Passing "_otl-recordings"/"_tcp" makes the browse silently return 0 results.
void RecorderServer::resolveUploadUrl() {
    upload_url = "";
    int n = MDNS.queryService("otl-recordings", "tcp");
    if (n > 0) {
        upload_url = String("http://") + MDNS.IP(0).toString() + ":" +
                     String(MDNS.port(0)) + "/api/uploads/recordings";
        Serial.printf("[NET] upload endpoint via mDNS: %s (host %s)\n",
                      upload_url.c_str(), MDNS.hostname(0).c_str());
        return;
    }
    if (strlen(HEADSPACE_UPLOAD_URL) > 0) {
        upload_url = HEADSPACE_UPLOAD_URL;
        Serial.printf("[NET] upload endpoint via fallback URL: %s\n", upload_url.c_str());
        return;
    }
    Serial.println("[NET] no upload endpoint (mDNS browse empty, no fallback URL) — uploads disabled");
}

// Extract an integer "bytes_written" value from a small JSON body like
// {"filename":"REC_001.wav","bytes_written":12345}. Returns -1 if absent.
// The response is a tiny fixed-shape object, so a string scan avoids pulling in
// a JSON library on the constrained device.
static long parse_bytes_written(const String& body) {
    int key = body.indexOf("\"bytes_written\"");
    if (key < 0) return -1;
    int colon = body.indexOf(':', key);
    if (colon < 0) return -1;
    int i = colon + 1;
    while (i < (int)body.length() && (body[i] == ' ' || body[i] == '\t')) i++;
    long val = 0;
    bool any = false;
    while (i < (int)body.length() && body[i] >= '0' && body[i] <= '9') {
        val = val * 10 + (body[i] - '0');
        any = true;
        i++;
    }
    return any ? val : -1;
}

// Push the oldest un-acked recording to Headspace. One file per call so the web
// server stays responsive between uploads. Streams the WAV body straight off SD
// (never buffered in RAM) and acks only on a 201 whose bytes_written matches the
// bytes sent — a confirmed disk write, not "bytes received".
// Count un-acked, non-empty recordings on the SD card. Called only when
// pending_dirty_ is set (boot, recording stop, resync) — never on a timer — so
// the cost stays off the UI refresh path.
void RecorderServer::recomputePending() {
    int n = 0;
    File dir = SD.open("/");
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            String base = basename_of(String(f.name()));
            if (!f.isDirectory() && is_recording_file(base) && f.size() > 44 &&
                !fileAcked(base)) {
                n++;
            }
            f.close();
        }
        dir.close();
    }
    pending_count_ = n;
}

// In-memory snapshot for the status-bar counter (no SD access — safe to call
// from the UI thread on every refresh).
UploadStatus RecorderServer::uploadStatus() {
    UploadStatus s;
    s.pending = (pending_count_ < 0) ? 0 : pending_count_;
    s.index   = burst_sent_ + 1;
    s.total   = burst_total_;
    uint32_t now = millis();
    if (sending_now_)             s.state = 1;  // POST in flight
    else if (now < done_until_)   s.state = 2;  // transient ✓ (all sent)
    else if (now < failed_until_) s.state = 3;  // transient ⚠ (cycle failed)
    else                          s.state = 0;  // idle (show backlog or nothing)
    return s;
}

void RecorderServer::uploadCycle() {
    last_upload    = millis();
    upload_pending = false;
    if (upload_url.length() == 0) return;  // no endpoint resolved

    // Find the first un-acked, non-empty recording (oldest-first by directory
    // order). One per cycle; the rest follow on subsequent ticks.
    String name;
    File dir = SD.open("/");
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            String base = basename_of(String(f.name()));
            if (!f.isDirectory() && is_recording_file(base) && f.size() > 44 &&
                !fileAcked(base)) {
                name = base;
                f.close();
                break;
            }
            f.close();
        }
        dir.close();
    }
    if (name.length() == 0) {  // nothing un-acked — burst complete/empty
        burst_total_ = 0;
        burst_sent_  = 0;
        return;
    }

    // First file of a fresh burst: snapshot how many are queued so the counter
    // can render "i/N" steadily across the whole burst.
    if (burst_total_ == 0) {
        burst_total_ = (pending_count_ > 0) ? pending_count_ : 1;
        burst_sent_  = 0;
    }

    String path = "/" + name;
    File file = SD.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[NET] upload: open failed %s\n", name.c_str());
        return;
    }
    size_t fileSize = file.size();

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(20000);  // server confirms after the disk write completes
    if (!http.begin(upload_url)) {
        Serial.println("[NET] upload: begin failed");
        file.close();
        return;
    }
    http.addHeader("Content-Type", "audio/wav");
    http.addHeader("X-Device-Id", REC_HOSTNAME);
    http.addHeader("X-Filename", name);
    http.addHeader("X-File-Size", String((uint32_t)fileSize));

    // Surface "⬆ i/N" before we block on the transfer. The POST holds the SPI bus
    // and freezes the UI for the whole stream, so PokeUpload() forces an immediate
    // paint+flush now — the 5s refresh timer can't catch it.
    sending_now_ = true;
    Page::StatusBar::PokeUpload();

    // Stream the file body off SD. Hold the shared SPI-bus mutex across the whole
    // transfer so SD reads can't interleave with an LCD flush on the shared bus.
    bus_take();
    int code = http.sendRequest("POST", &file, fileSize);
    bus_give();
    String resp = http.getString();
    http.end();
    file.close();
    sending_now_ = false;

    if (code == 201) {
        long written = parse_bytes_written(resp);
        if (written == (long)fileSize) {
            ackFile(name);
            if (pending_count_ > 0) pending_count_--;
            burst_sent_++;
            Serial.printf("[NET] uploaded + acked %s (%u bytes) [%d/%d]\n",
                          name.c_str(), (uint32_t)fileSize, burst_sent_, burst_total_);
            if (pending_count_ <= 0) {
                // Whole backlog cleared — transient ✓, then the counter hides.
                burst_total_ = 0;
                burst_sent_  = 0;
                done_until_  = millis() + 2000;
                Page::StatusBar::PokeUpload(true);
            } else {
                upload_pending = true;  // immediately try the next un-acked file
                Page::StatusBar::PokeUpload();
            }
        } else {
            Serial.printf("[NET] upload %s: 201 but bytes_written=%ld != %u — not acked\n",
                          name.c_str(), written, (uint32_t)fileSize);
            failed_until_ = millis() + 2000;
            Page::StatusBar::PokeUpload(true);
        }
    } else {
        Serial.printf("[NET] upload %s failed, http=%d — retry next cycle\n",
                      name.c_str(), code);
        failed_until_ = millis() + 2000;
        Page::StatusBar::PokeUpload(true);
    }
}

}  // namespace Net
