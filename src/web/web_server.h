#ifndef WEB_SERVER_H
#define WEB_SERVER_H

// ============================================================================
// Web server xem truc tiep va chinh cam bien
// ============================================================================
// Cong 80  : trang dieu khien, /capture, /status, /control, /reg, /pll...
// Cong 81  : /stream (MJPEG)
//
// Chay trong task rieng cua esp_http_server, khong lien quan gi toi duong
// bam nut -> chup -> gui server cua audio_service.*.
// ============================================================================

// Dung ca hai server len. Chi goi khi da co IP.
void startCameraServer();

#endif  // WEB_SERVER_H
