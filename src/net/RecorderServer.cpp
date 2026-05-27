#include "RecorderServer.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <SD.h>

// WiFi credentials + Headspace URL are optional at build time: the public repo
// compiles without include/secrets.h (recorder just runs offline). Provide it
// to enable WiFi serving and Headspace notifications.
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
#ifndef HEADSPACE_NOTIFY_URL
#  define HEADSPACE_NOTIFY_URL ""  // empty -> notifications disabled
#endif

// Filenames already accepted by Headspace, one per line. Lets the device avoid
// re-notifying for the same recording across reboots.
#define UPLOADED_MARKER "/UPLOADED.txt"
#define NOTIFY_RETRY_MS 30000UL  // back-off between notify attempts while anything's un-acked

namespace Net {

RecorderServer Server;

static bool creds_present() { return strlen(WIFI_SSID) > 0; }
static bool notify_enabled() { return strlen(HEADSPACE_NOTIFY_URL) > 0; }

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

    bool connected = (WiFi.status() == WL_CONNECTED);

    // Bring up mDNS + HTTP once, the first time WiFi comes up.
    if (connected && !started) {
        if (MDNS.begin(REC_HOSTNAME)) {
            MDNS.addService("http", "tcp", 80);
            MDNS.addServiceTxt("http", "tcp", "path", "/api/recordings");
        }
        routes();
        server.begin();
        started        = true;
        notify_pending = true;  // announce anything un-uploaded as soon as we're up
        Serial.printf("[NET] Up: http://%s.local  (%s)\n", REC_HOSTNAME,
                      WiFi.localIP().toString().c_str());
    }

    if (started) server.handleClient();

    // Notify Headspace when idle — never mid-recording (would stall capture).
    if (started && !recording &&
        (notify_pending || (millis() - last_notify > NOTIFY_RETRY_MS))) {
        notifyHeadspace();
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
    server.streamFile(f, "audio/wav");
    f.close();
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

// POST {device, address, recordings:[un-acked, non-empty]} to Headspace.
// On 200 {"pulled":[...]}, ack each pulled file so we don't re-notify.
void RecorderServer::notifyHeadspace() {
    last_notify    = millis();
    notify_pending = false;
    if (!notify_enabled()) return;

    // Collect un-acked, non-empty recordings into a JSON array string.
    String list;
    int count = 0;
    File dir = SD.open("/");
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            String base = basename_of(String(f.name()));
            if (!f.isDirectory() && is_recording_file(base) && f.size() > 44 &&
                !fileAcked(base)) {
                if (count++) list += ",";
                list += "\"" + base + "\"";
            }
            f.close();
        }
        dir.close();
    }
    if (count == 0) return;  // nothing new to announce

    String body = String("{\"device\":\"") + REC_HOSTNAME +
                  "\",\"address\":\"" + WiFi.localIP().toString() +
                  "\",\"recordings\":[" + list + "]}";

    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(6000);
    if (!http.begin(HEADSPACE_NOTIFY_URL)) {
        Serial.println("[NET] notify: begin failed");
        return;  // last_notify already set -> retry on the next back-off tick
    }
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    if (code == 200) {
        String resp = http.getString();
        // Ack only files reported in the "pulled" array; fall back to acking all
        // we sent if the response shape is unexpected (200 == success).
        int pulled  = resp.indexOf("\"pulled\"");
        int skipped = resp.indexOf("\"skipped\"");
        int from = 0;
        while (true) {
            int q1 = list.indexOf('"', from);
            if (q1 < 0) break;
            int q2 = list.indexOf('"', q1 + 1);
            if (q2 < 0) break;
            String name = list.substring(q1 + 1, q2);
            from = q2 + 1;
            bool ack;
            if (pulled < 0) {
                ack = true;  // no "pulled" key, but 200 -> treat as accepted
            } else {
                int at = resp.indexOf("\"" + name + "\"", pulled);
                ack = (at >= 0) && (skipped < 0 || at < skipped);
            }
            if (ack) ackFile(name);
        }
        Serial.printf("[NET] notify OK (%d new): %s\n", count, resp.c_str());
    } else {
        // last_notify already set at the top -> retries on the next back-off tick.
        Serial.printf("[NET] notify failed: HTTP %d — retry in %lus\n", code,
                      NOTIFY_RETRY_MS / 1000);
    }
    http.end();
}

}  // namespace Net
