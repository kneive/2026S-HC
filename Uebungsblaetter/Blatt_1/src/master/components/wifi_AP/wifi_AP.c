#include "wifi_AP.h"

#include <stdio.h>
#include <string.h>
#include "camera.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char *TAG = "WIFI_AP";

#define WIFI_AP_SSID "ESP32S3-Camera"
#define WIFI_AP_PASSWORD "camera123"
#define WIFI_AP_CHANNEL 6
#define WIFI_AP_MAX_CONNECTIONS 4

#define STREAM_BOUNDARY "123456789000000000000987654321"
#define STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY
#define STREAM_BOUNDARY_LINE "\r\n--" STREAM_BOUNDARY "\r\n"
#define WIFI_AP_STARTED_BIT BIT0

static httpd_handle_t web_server = NULL;
static httpd_handle_t stream_server = NULL;
static EventGroupHandle_t wifi_event_group = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  (void)arg;
  (void)event_data;

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START && wifi_event_group != NULL) {
    xEventGroupSetBits(wifi_event_group, WIFI_AP_STARTED_BIT);
  }
}

static esp_err_t init_nvs(void)
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
    ret = nvs_flash_init();
  }

  return ret;
}

static esp_err_t root_handler(httpd_req_t *req)
{
  static const char html[] =
      "<!doctype html>"
      "<html>"
      "<head>"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>ESP32-S3 Camera</title>"
      "<style>"
      "body{margin:0;background:#101418;color:#eef2f5;font-family:Arial,sans-serif;display:grid;place-items:center;min-height:100vh}"
      "main{width:min(100vw,960px);padding:16px;box-sizing:border-box}"
      "h1{font-size:22px;font-weight:600;margin:0 0 12px}"
      ".stage{position:relative;background:#000;line-height:0}"
      "img{width:100%;height:auto;display:block}"
      "canvas{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}"
      "p{color:#aab4be;font-size:14px}"
      "</style>"
      "</head>"
      "<body>"
      "<main>"
      "<h1>ESP32-S3 Camera Stream</h1>"
      "<div class=\"stage\">"
      "<img id=\"stream\" alt=\"camera stream\">"
      "<canvas id=\"overlay\"></canvas>"
      "</div>"
      "<p id=\"status\">Targets: 0</p>"
      "<p>Connect to WiFi SSID ESP32S3-Camera and open http://192.168.4.1/</p>"
      "<script>"
      "const img=document.getElementById('stream'),canvas=document.getElementById('overlay'),ctx=canvas.getContext('2d');"
      "const status=document.getElementById('status');"
      "let targets=[],fw=640,fh=480;"
      "img.src='http://'+location.hostname+':81/stream';"
      "function size(){const r=img.getBoundingClientRect();canvas.width=Math.max(1,Math.round(r.width));canvas.height=Math.max(1,Math.round(r.height));}"
      "function mac(t){return t.mac||'';}"
      "function draw(){size();ctx.clearRect(0,0,canvas.width,canvas.height);const sx=canvas.width/fw,sy=canvas.height/fh;ctx.lineWidth=2;ctx.font='12px Arial';ctx.textBaseline='top';for(const t of targets){const x=t.x*sx,y=t.y*sy;ctx.strokeStyle='#00e5ff';ctx.fillStyle='rgba(0,0,0,.55)';ctx.beginPath();ctx.arc(x,y,10,0,Math.PI*2);ctx.stroke();ctx.beginPath();ctx.moveTo(x-16,y);ctx.lineTo(x+16,y);ctx.moveTo(x,y-16);ctx.lineTo(x,y+16);ctx.stroke();const label=mac(t)+' '+t.rssi+'dBm';const w=ctx.measureText(label).width+8;ctx.fillRect(x+12,y+12,w,18);ctx.fillStyle='#fff';ctx.fillText(label,x+16,y+15);}}"
      "async function poll(){try{const r=await fetch('/targets.json',{cache:'no-store'});const j=await r.json();targets=j.targets||[];fw=j.frame_width||640;fh=j.frame_height||480;status.textContent='Targets: '+targets.length;draw();}catch(e){status.textContent='Targets: fetch failed';}}"
      "window.addEventListener('resize',draw);setInterval(poll,250);poll();"
      "</script>"
      "</main>"
      "</body>"
      "</html>";

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t targets_handler(httpd_req_t *req)
{
  camera_target_t targets[CAMERA_MAX_TARGETS];
  size_t count = camera_get_targets(targets, CAMERA_MAX_TARGETS);
  char json[3072];
  int offset = snprintf(json, sizeof(json),
                        "{\"frame_width\":%d,\"frame_height\":%d,\"targets\":[",
                        CAMERA_TARGET_FRAME_WIDTH, CAMERA_TARGET_FRAME_HEIGHT);

  for (size_t i = 0; i < count && offset > 0 && offset < (int)sizeof(json); i++) {
    int written = snprintf(json + offset, sizeof(json) - offset,
                           "%s{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"x\":%d,\"y\":%d,\"rssi\":%d}",
                           (i == 0) ? "" : ",",
                           targets[i].mac[0], targets[i].mac[1], targets[i].mac[2],
                           targets[i].mac[3], targets[i].mac[4], targets[i].mac[5],
                           targets[i].x, targets[i].y, targets[i].rssi);
    if (written < 0) {
      return ESP_FAIL;
    }
    offset += written;
  }

  if (offset < 0 || offset >= (int)sizeof(json)) {
    return ESP_ERR_NO_MEM;
  }

  int written = snprintf(json + offset, sizeof(json) - offset, "]}");
  if (written < 0 || offset + written >= (int)sizeof(json)) {
    return ESP_ERR_NO_MEM;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stream_handler(httpd_req_t *req)
{
  esp_err_t ret = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (ret != ESP_OK) {
    return ret;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");

  while (true) {
    camera_fb_t *frame = camera_capture_frame();
    if (frame == NULL) {
      ESP_LOGW(TAG, "Camera frame capture failed");
      return ESP_FAIL;
    }

    char part_header[96];
    int header_len = snprintf(part_header, sizeof(part_header),
                              "Content-Type: image/jpeg\r\n"
                              "Content-Length: %zu\r\n\r\n",
                              frame->len);

    ret = httpd_resp_send_chunk(req, STREAM_BOUNDARY_LINE, strlen(STREAM_BOUNDARY_LINE));
    if (ret == ESP_OK) {
      ret = httpd_resp_send_chunk(req, part_header, header_len);
    }
    if (ret == ESP_OK) {
      ret = httpd_resp_send_chunk(req, (const char *)frame->buf, frame->len);
    }

    camera_return_frame(frame);

    if (ret != ESP_OK) {
      ESP_LOGI(TAG, "Camera stream client disconnected");
      break;
    }
  }

  return ret;
}

static esp_err_t start_web_server(void)
{
  if (web_server != NULL) {
    return ESP_OK;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.stack_size = 8192;

  ESP_RETURN_ON_ERROR(httpd_start(&web_server, &config), TAG, "HTTP web server start failed");

  httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
    .user_ctx = NULL,
  };
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(web_server, &root_uri), TAG, "Root URI registration failed");

  httpd_uri_t targets_uri = {
    .uri = "/targets.json",
    .method = HTTP_GET,
    .handler = targets_handler,
    .user_ctx = NULL,
  };
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(web_server, &targets_uri), TAG, "Targets URI registration failed");

  ESP_LOGI(TAG, "Camera overlay page ready at http://192.168.4.1/");
  return ESP_OK;
}

static esp_err_t start_stream_server(void)
{
  if (stream_server != NULL) {
    return ESP_OK;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 81;
  config.ctrl_port = 32769;
  config.stack_size = 8192;

  ESP_RETURN_ON_ERROR(httpd_start(&stream_server, &config), TAG, "HTTP stream server start failed");

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL,
  };
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(stream_server, &stream_uri), TAG, "Stream URI registration failed");

  ESP_LOGI(TAG, "Camera stream ready at http://192.168.4.1:81/stream");
  return ESP_OK;
}

esp_err_t wifi_AP_init(void)
{
  ESP_RETURN_ON_ERROR(init_nvs(), TAG, "NVS init failed");
  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Network interface init failed");

  esp_err_t event_ret = esp_event_loop_create_default();
  if (event_ret != ESP_OK && event_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Event loop init failed: %s", esp_err_to_name(event_ret));
    return event_ret;
  }

  esp_netif_create_default_wifi_ap();

  if (wifi_event_group == NULL) {
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
      ESP_LOGE(TAG, "WiFi event group creation failed");
      return ESP_ERR_NO_MEM;
    }
  }

  ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                         WIFI_EVENT_AP_START,
                                                         wifi_event_handler,
                                                         NULL,
                                                         NULL),
                      TAG, "WiFi AP start handler registration failed");

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "WiFi init failed");

  wifi_config_t wifi_config = {
    .ap = {
      .ssid = WIFI_AP_SSID,
      .ssid_len = strlen(WIFI_AP_SSID),
      .channel = WIFI_AP_CHANNEL,
      .password = WIFI_AP_PASSWORD,
      .max_connection = WIFI_AP_MAX_CONNECTIONS,
      .authmode = WIFI_AUTH_WPA2_PSK,
      .pmf_cfg = {
        .required = false,
      },
    },
  };

  if (strlen(WIFI_AP_PASSWORD) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }

  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "WiFi AP mode failed");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "WiFi AP config failed");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WiFi AP start failed");

  EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                         WIFI_AP_STARTED_BIT,
                                         pdTRUE,
                                         pdFALSE,
                                         pdMS_TO_TICKS(5000));
  if ((bits & WIFI_AP_STARTED_BIT) == 0) {
    ESP_LOGE(TAG, "Timed out waiting for WiFi AP start event");
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(TAG, "WiFi AP started. SSID: %s, password: %s, URL: http://192.168.4.1/",
           WIFI_AP_SSID, WIFI_AP_PASSWORD);

  ESP_RETURN_ON_ERROR(start_web_server(), TAG, "Web server start failed");
  return start_stream_server();
}
