#ifndef __RECORDER_SERVER_H
#define __RECORDER_SERVER_H

#include <Arduino.h>
#include <WebServer.h>

namespace Net {

// Exposes the SD-card recordings over WiFi so a Headspace agent (Mick) can
// discover the device (mDNS: http://core-s3.local) and pull WAVs:
//   GET /api/recordings   -> JSON list [{name,size}, ...]
//   GET /rec/<name>       -> the WAV file (audio/wav)
// All endpoints return 503 while a recording is in progress, so serving never
// stalls the real-time capture loop (single-threaded with lv_timer_handler).
class RecorderServer {
   public:
    void begin();                 // start WiFi STA (no-op if no creds compiled in)
    void loop();                  // bring up mDNS/HTTP once connected; service clients
    void setRecording(bool r) { recording = r; }
    bool wifiConnected();
    String hostUrl();             // "http://core-s3.local"

   private:
    WebServer server{80};
    bool recording      = false;
    bool started        = false;  // mDNS + HTTP started (once, on first connect)
    uint32_t last_retry = 0;

    void routes();
    void handleList();
    void handleDownload();
    void handleRoot();
};

extern RecorderServer Server;

}  // namespace Net

#endif
