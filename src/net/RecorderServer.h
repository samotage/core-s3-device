#ifndef __RECORDER_SERVER_H
#define __RECORDER_SERVER_H

#include <Arduino.h>
#include <WebServer.h>

namespace Net {

// Two halves:
//  1. Serves the SD recordings over WiFi (mDNS http://core-s3.local) so
//     Headspace can pull them: GET /api/recordings, GET /rec/<name>.
//  2. Notifies Headspace when it has un-uploaded recordings: POSTs a small JSON
//     ping to HEADSPACE_NOTIFY_URL; Headspace then pulls + transcribes.
// All serving endpoints return 503 while recording, so neither serving nor the
// notify can stall the single-threaded real-time capture loop.
class RecorderServer {
   public:
    void begin();                 // start WiFi STA (no-op if no creds compiled in)
    void loop();                  // bring up mDNS/HTTP; service clients; notify
    void setRecording(bool r) { recording = r; }
    void requestNotify() { notify_pending = true; }  // ping Headspace asap (when idle)
    bool wifiConnected();
    String hostUrl();             // "http://core-s3.local"

   private:
    WebServer server{80};
    bool recording      = false;
    bool started        = false;  // mDNS + HTTP started (once, on first connect)
    bool notify_pending = false;
    bool was_paused     = false;  // tracks the "paused during recording" state
    uint32_t last_retry  = 0;
    uint32_t last_notify = 0;

    void routes();
    void handleList();
    void handleDownload();
    void handleRoot();

    void notifyHeadspace();              // POST the list of un-acked recordings
    bool fileAcked(const String& name);  // is name in the SD uploaded-marker?
    void ackFile(const String& name);    // append name to the marker
};

extern RecorderServer Server;

}  // namespace Net

#endif
