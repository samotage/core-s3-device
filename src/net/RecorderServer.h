#ifndef __RECORDER_SERVER_H
#define __RECORDER_SERVER_H

#include <Arduino.h>
#include <WebServer.h>

namespace Net {

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
    void setRecording(bool r) { recording = r; }
    void requestUpload() { upload_pending = true; }  // run upload cycle asap (when idle)
    bool wifiConnected();
    String hostUrl();             // "http://core-s3.local"

   private:
    WebServer server{80};
    bool recording      = false;
    bool started        = false;  // mDNS + HTTP started (once, on first connect)
    bool upload_pending = false;  // run an upload cycle as soon as idle
    bool was_paused     = false;  // tracks the "paused during recording" state
    uint32_t last_retry  = 0;
    uint32_t last_upload = 0;
    String upload_url;            // resolved upload endpoint (mDNS or fallback)

    void routes();
    void handleList();
    void handleDownload();
    void handleRoot();

    void resolveUploadUrl();             // mDNS browse -> upload_url, else fallback
    void uploadCycle();                  // push un-acked recordings (one per call)
    bool fileAcked(const String& name);  // is name in the SD uploaded-marker?
    void ackFile(const String& name);    // append name to the marker
};

extern RecorderServer Server;

}  // namespace Net

#endif
