#ifndef __RECORDER_SERVER_H
#define __RECORDER_SERVER_H

#include <Arduino.h>
#include <WebServer.h>

namespace fs { class File; }  // fwd-decl: postFileChunked takes a File& (defined in the .cpp via SD.h)

namespace Net {

// Snapshot of upload progress for the status bar. state: 0 idle, 1 sending,
// 2 done (all sent — transient), 3 failed (last cycle — transient). index/total
// are the 1-based file position within the current push burst (valid when
// sending); pending is the count of un-acked recordings on the SD card.
struct UploadStatus {
    int pending;
    int index;
    int total;
    uint8_t state;
};

// Two halves:
//  1. Serves the SD recordings over WiFi (mDNS http://core-s3.local) so
//     Headspace can pull them for diagnostics: GET /api/recordings,
//     GET /rec/<name>. (Retained, no longer the primary transfer path.)
//  2. Pushes un-uploaded recordings to Headspace: discovers the upload endpoint
//     via mDNS service browse (_otl-recordings._tcp) and POSTs each WAV body
//     raw, acking only on a confirmed-disk 201.
// All serving endpoints return 503 while recording, and uploads only run when
// idle + connected, so neither can stall the single-threaded capture loop.
class RecorderServer {
   public:
    void begin();                 // start WiFi STA (no-op if no creds compiled in)
    void loop();                  // bring up mDNS/HTTP; service clients; upload
    // Stopping a recording leaves a new (un-acked) file on the card, so flag the
    // pending count for recompute on the next idle pass.
    void setRecording(bool r) { recording = r; if (!r) pending_dirty_ = true; }
    void requestUpload() { upload_pending = true; }  // run upload cycle asap (when idle)

    // Test/ops harness: start a FIXED-DURATION recording remotely.
    //
    // The duration is set up front and cannot be changed later, because the radio
    // is powered down for the whole capture (see loop()) — once recording starts
    // the device is unreachable until it stops itself. This is the only way to
    // drive a hands-off soak: app Serial lives on UART0 (ARDUINO_USB_CDC_ON_BOOT
    // was dropped in 4f148f3 to fix battery cold-start), so the r/s serial
    // controls are not reachable over USB.
    //
    // Returns true once, handing the caller the requested duration; AppRecorder
    // consumes it and owns the auto-stop deadline.
    bool consumeRecordRequest(uint32_t& secs);
    bool wifiConnected();
    String hostUrl();             // "http://core-s3.local"
    UploadStatus uploadStatus();  // snapshot for the status bar counter

   private:
    WebServer server{80};
    bool recording      = false;
    bool started        = false;  // mDNS + HTTP started (once, on first connect)
    bool upload_pending = false;  // run an upload cycle as soon as idle
    bool was_paused     = false;  // tracks the "paused during recording" state
    uint32_t last_retry  = 0;
    uint32_t last_upload = 0;
    String upload_url;            // resolved upload endpoint (mDNS or fallback)

    // Pending remote record request (see consumeRecordRequest). Armed with a short
    // delay so the HTTP 200 drains before the capture starts and kills the radio.
    uint32_t record_req_secs_  = 0;
    uint32_t record_req_at_ms_ = 0;

    // Upload-counter state (status bar). pending_count_ is maintained in memory
    // (recomputed by SD scan only when dirty) so the UI never scans the card.
    int  pending_count_  = -1;    // un-acked recordings on SD (-1 = not computed)
    bool pending_dirty_  = true;  // recompute pending_count_ on next idle pass
    int  burst_total_    = 0;     // files to send in the current push burst (the N)
    int  burst_sent_     = 0;     // files acked so far in the current burst
    bool sending_now_    = false; // a POST is in flight right now
    int  fail_streak_    = 0;     // consecutive failed cycles -> retry backoff
    uint32_t done_until_   = 0;   // millis() until which the ✓ "all sent" holds
    uint32_t failed_until_ = 0;   // millis() until which the ⚠ failed holds

    void routes();
    void handleList();
    void handleDownload();
    void handleRoot();
    void handleDiag();   // boot summary + /DIAG.log over the LAN (no serial needed)

    void resolveUploadUrl();             // mDNS browse -> upload_url, else fallback
    void uploadCycle();                  // push un-acked recordings (one per call)
    // Stream one file to the upload endpoint over a raw socket, reading the card
    // in small blocks and servicing LVGL between blocks so the UI never freezes
    // mid-transfer. Returns the HTTP status (<0 on transport error); sets
    // out_written from the 201 JSON body.
    int  postFileChunked(const String& name, fs::File& file, size_t fileSize, long& out_written);
    void recomputePending();             // SD scan -> pending_count_
    bool fileAcked(const String& name);  // is name in the SD uploaded-marker?
    void ackFile(const String& name);    // append name to the marker
};

extern RecorderServer Server;

}  // namespace Net

#endif
