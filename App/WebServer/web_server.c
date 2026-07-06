#include "web_server.h"
#include "mongoose.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// ================== 配置与状态 ==================
#define MAX_SESSIONS 32
#define SESSION_ID_LEN 32
#define MAX_LOGIN_LEN 32

// 登录账号密码（默认值为 admin/123456）
static char g_login_username[MAX_LOGIN_LEN] = "admin";
static char g_login_password[MAX_LOGIN_LEN] = "123456";

// 当前状态（视频参数使用枚举常量）
static device_state_t current_state = {
    .version = "1.0.0",
    .ip_mode = 0,
    .ip_addr = "192.168.1.100",
    .gateway = "192.168.1.1",
    .netmask = "255.255.255.0",
    .dns = "8.8.8.8",
    .gb_code = "34020000001320000001",
    .gb_ip = "192.168.1.200",
    .gb_port = "5060",
    .gb_domain = "0000000000",
    .gb_user_id = "34020000001320000001",
    .gb_channel_code = "34020000001320000001",
    .gb_device_id = "34020000001320000001",
    .gb_password = "123456",
    .main_stream = {
        .video_codec = VIDEO_CODEC_H265,
        .bitrate = 2048,
        .framerate = 30,
        .gop = 30
    },
    .sub_stream = {
        .video_codec = VIDEO_CODEC_H265,
        .bitrate = 512,
        .framerate = 25,
        .gop = 25
    },
    .audio_codec = AUDIO_CODEC_G711A,
    .night_mode = NIGHT_MODE_DEFAULT,
    .gb_enable = 0,
    .gb_config_mode = 0,
    .gb_status = GB_STATUS_OFFLINE,
    .gat1400_enable = 0,
    .gat1400_ip = "183.252.186.166",
    .gat1400_port = "33855",
    .gat1400_ipv6 = "",
    .gat1400_ipv6_port = "0",
    .gat1400_user = "admin",
    .gat1400_device_id = "30510200001190000010",
    .gat1400_password = "Dz8h6kM9",
    .gat1400_status = GAT1400_STATUS_OFFLINE,
    // WiFi 配置默认值
    .wifi_need_config = 0,
    .wifi_ssid = "",
    .wifi_password = "",
    // SD卡配置默认值
    .sd_format_status = SD_FORMAT_IDLE
};

typedef struct {
    char id[SESSION_ID_LEN + 1];
    int valid;
} session_t;
static session_t sessions[MAX_SESSIONS];
static int session_count = 0;

// ================== 登录账号密码设置接口 ==================
void set_login_credentials(const char *username, const char *password) {
    if (username != NULL) {
        strncpy(g_login_username, username, MAX_LOGIN_LEN - 1);
        g_login_username[MAX_LOGIN_LEN - 1] = '\0';
    }
    if (password != NULL) {
        strncpy(g_login_password, password, MAX_LOGIN_LEN - 1);
        g_login_password[MAX_LOGIN_LEN - 1] = '\0';
    }
}

// ================== Web Server 模块内部变量 ==================
static struct mg_mgr mgr;
static volatile int web_running = 0;
static volatile int web_stop_requested = 0;   // 请求停止标志

// ================== 辅助函数：枚举转字符串 ==================
static const char* codec_to_str(video_codec_t codec) {
    return (codec == VIDEO_CODEC_H265) ? "h265" : "h264";
}

static video_codec_t str_to_codec(const char *str) {
    if (str && strcmp(str, "h265") == 0) return VIDEO_CODEC_H265;
    return VIDEO_CODEC_H264;
}

// ================== 音频编码枚举转字符串 ==================
static const char* audio_codec_to_str(audio_codec_t codec) {
    switch (codec) {
        case AUDIO_CODEC_G711A: return "g711a";
        case AUDIO_CODEC_G711U: return "g711u";
        case AUDIO_CODEC_AAC:  return "aac";
        default: return "g711a";
    }
}

static audio_codec_t str_to_audio_codec(const char *str) {
    if (str) {
        if (strcmp(str, "g711a") == 0) return AUDIO_CODEC_G711A;
        if (strcmp(str, "g711u") == 0) return AUDIO_CODEC_G711U;
        if (strcmp(str, "aac") == 0)  return AUDIO_CODEC_AAC;
    }
    return AUDIO_CODEC_G711A;
}

// ================== 夜视模式枚举转字符串 ==================
static const char* night_mode_to_str(night_mode_t mode) {
    switch (mode) {
        case NIGHT_MODE_WHITE_LIGHT: return "white_light";
        case NIGHT_MODE_INFRARED:   return "infrared";
        case NIGHT_MODE_DEFAULT:    return "default";
        default: return "default";
    }
}

static night_mode_t str_to_night_mode(const char *str) {
    if (str) {
        if (strcmp(str, "white_light") == 0) return NIGHT_MODE_WHITE_LIGHT;
        if (strcmp(str, "infrared") == 0)   return NIGHT_MODE_INFRARED;
        if (strcmp(str, "default") == 0)    return NIGHT_MODE_DEFAULT;
    }
    return NIGHT_MODE_DEFAULT;
}

// ================== 页面生成函数 ==================
static const char* get_login_page(void) {
    return
        "<!DOCTYPE html>"
        "<html lang='zh-CN'><head><meta charset='UTF-8'>"
        "<title>登录</title>"
        "<style>"
        "* { margin:0; padding:0; box-sizing:border-box; }"
        "body { background:#e0e0e0; font-family:sans-serif; min-height:100vh; display:flex; justify-content:center; align-items:center; }"
        ".login-container { display:flex; background:white; border-radius:8px; box-shadow:0 2px 10px rgba(0,0,0,0.1); width:800px; height:500px; overflow:hidden; }"
        ".left-panel { background:#007bff; color:white; padding:60px 40px; display:flex; flex-direction:column; align-items:center; flex:1.5; text-align:center; position:relative; justify-content:flex-start; }"
        ".content-container { display:flex; flex-direction:column; align-items:center; justify-content:center; flex:1; width:100%; }"
        ".right-panel { padding:60px 40px; display:flex; flex-direction:column; justify-content:center; flex:1; }"
        ".company-name { font-size:32px; font-weight:bold; margin-bottom:15px; }"
        ".device-id { font-size:18px; opacity:0.9; margin-bottom:20px; }"
        ".copyright { font-size:12px; opacity:0.7; text-align:center; line-height:1.5; position:absolute; bottom:50px; left:0; right:0; }"
        "h1 { text-align:center; margin-bottom:20px; font-size:24px; }"
        "form { display:flex; flex-direction:column; }"
        "label { margin:8px 0 4px 0; font-weight:bold; }"
        "input[type=text], input[type=password] { padding:6px; border:1px solid #ccc; border-radius:4px; width:100%%; text-align:left; }"
        "input[type=submit] { background:#007bff; color:white; border:none; padding:10px; border-radius:4px; cursor:pointer; margin-top:10px; }"
        "#error-msg { color:red; text-align:center; margin-top:10px; font-size:14px; min-height:24px; }"
        "</style>"
        "</head><body>"
        "<div class='login-container'>"
        "<div class='left-panel'>"
        "<div class='content-container'>"
        "<div class='company-name'>中国移动</div>"
        "<div class='device-id'>C4611</div>"
        "</div>"
        "<div class='copyright'>中国移动通信集团有限公司政企客户分公司 版权所有<br>客服电话:400-1103-888</div>"
        "</div>"
        "<div class='right-panel'>"
        "<h1>欢迎登录</h1>"
        "<form id='loginForm'>"
        "<label>用户名：</label><input type='text' name='username' id='username'>"
        "<label>密码：</label><input type='password' name='password' id='password'>"
        "<input type='submit' value='登录'>"
        "</form>"
        "<div id='error-msg'></div>"
        "</div>"
        "</div>"
        "<script>"
        "document.getElementById('loginForm').addEventListener('submit', function(e){"
        "  e.preventDefault();"
        "  var username = document.getElementById('username').value;"
        "  var password = document.getElementById('password').value;"
        "  var errorDiv = document.getElementById('error-msg');"
        "  fetch('/login', {"
        "    method: 'POST',"
        "    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },"
        "    body: 'username=' + encodeURIComponent(username) + '&password=' + encodeURIComponent(password)"
        "  })"
        "  .then(response => {"
        "    if (response.redirected) {"
        "      window.location.href = response.url;"
        "    } else {"
        "      return response.json();"
        "    }"
        "  })"
        "  .then(data => {"
        "    if (data && data.error) {"
        "      errorDiv.textContent = data.error;"
        "      setTimeout(function() { errorDiv.textContent = ''; }, 1000);"
        "    }"
        "  })"
        "  .catch(err => console.error(err));"
        "});"
        "</script>"
        "</body></html>";
}

static const char* get_login_fail_page(void) {
    return
        "<!DOCTYPE html>"
        "<html lang='zh-CN'><head><meta charset='UTF-8'>"
        "<title>登录失败</title>"
        "<style>"
        "body{font-family:sans-serif;margin:2em;text-align:center;}"
        "</style>"
        "</head><body>"
        "<h1>登录失败</h1>"
        "<p>用户名或密码错误。</p>"
        "<a href='/login'>返回登录</a>"
        "</body></html>";
}

static char* get_config_page(const device_state_t *state) {
    const char *main_codec_str = codec_to_str(state->main_stream.video_codec);
    const char *sub_codec_str = codec_to_str(state->sub_stream.video_codec);
    const char *audio_codec_str = audio_codec_to_str(state->audio_codec);
    const char *night_mode_str = night_mode_to_str(state->night_mode);
    const char *gb_enable_checked = (state->gb_enable == 1) ? "checked" : "";
    const char *wifi_checked = (state->wifi_need_config == 1) ? "checked" : "";
    int main_bitrate = state->main_stream.bitrate;
    int main_framerate = state->main_stream.framerate;
    int main_gop = state->main_stream.gop;
    int sub_bitrate = state->sub_stream.bitrate;
    int sub_framerate = state->sub_stream.framerate;
    int sub_gop = state->sub_stream.gop;

    const char *fmt =
        "<!DOCTYPE html>"
        "<html lang='zh-CN'><head><meta charset='UTF-8'>"
        "<title>摄像头配置</title>"
        "<style>"
        "* { margin:0; padding:0; box-sizing:border-box; }"
        "body { background:#f5f5f5; font-family:sans-serif; min-height:100vh; display:flex; justify-content:center; align-items:center; }"
        ".container { background:white; border-radius:8px; box-shadow:0 2px 10px rgba(0,0,0,0.1); padding:30px 40px; width:800px; }"
        "h1 { text-align:center; margin-bottom:20px; color:#333; }"
        "p { text-align:center; margin-bottom:25px; font-weight:bold; }"
        "fieldset { margin-bottom:20px; border:1px solid #ddd; border-radius:4px; padding:15px; }"
        "legend { font-weight:bold; padding:0 10px; width:auto; }"
        "label { font-weight:bold; margin-bottom:5px; display:block; }"
        "input[type=text], input[type=number], select { padding:6px; border-radius:4px; border:1px solid #ccc; margin-bottom:10px; width:100%%; }"
        ".radio-group { margin:10px 0; display:flex; gap:12px; align-items:center; flex-wrap:wrap; }"
        ".radio-group span { font-weight:bold; margin-right:5px; white-space:nowrap; }"
        ".radio-group label { font-weight:normal; display:inline-block; margin-right:12px; cursor:pointer; }"
        ".radio-group input[type=\"radio\"] { margin-right:5px; }"
        "input[type=\"radio\"]:checked { accent-color: #007bff; }"
        "input[type=\"checkbox\"]:checked { accent-color: #007bff; }"
        ".static-fields { margin-top:10px; }"
        ".button-row { display:flex; justify-content:center; gap:20px; margin-top:20px; }"
        ".button-row button { width:120px; padding:10px; border:none; border-radius:4px; cursor:pointer; font-size:16px; }"
        ".save-btn { background:#007bff; color:white; }"
        ".logout-btn { background:#f44336; color:white; }"
        "#sdFormatBtn { padding:8px 16px; border:none; border-radius:4px; cursor:pointer; background:#ff9800; color:white; font-size:14px; }"
        "#sdFormatBtn:hover { background:#f57c00; }"
        "#sdFormatStatus { font-size:14px; font-weight:bold; }"
        ".message { margin-top:10px; text-align:center; font-size:14px; min-height:30px; }"
        ".success { color:#4CAF50; }"
        ".error { color:#f44336; }"
        ".ip-error { color:#f44336; font-size:12px; margin-top:-8px; margin-bottom:0px; min-height:18px; }"
        ".config-row { display:flex; gap:20px; flex-wrap:nowrap; }"
        ".config-row fieldset { flex:1; min-width:250px; }"
        "</style>"
        "<script>"
        "var currentMainVideoCodec = '%s';"
        "var currentMainBitrate = %d;"
        "var currentMainFramerate = %d;"
        "var currentMainGop = %d;"
        "var currentSubVideoCodec = '%s';"
        "var currentSubBitrate = %d;"
        "var currentSubFramerate = %d;"
        "var currentSubGop = %d;"
        "var currentAudioCodec = '%s';"
        "var currentNightMode = '%s';"
        "function toggleStaticFields() {"
        "  var mode = document.querySelector('input[name=\"ip_mode\"]:checked').value;"
        "  var staticDiv = document.getElementById('staticFields');"
        "  if (mode === 'static') { staticDiv.style.display = 'block'; }"
        "  else { staticDiv.style.display = 'none'; }"
        "}"
        "function updateMainBitrateOptions() {"
        "  var videoCodec = document.querySelector('input[name=\"main_video_codec\"]:checked').value;"
        "  var bitrateSelect = document.getElementById('main_bitrate');"
        "  var options = [];"
        "  if (videoCodec === 'h265') {"
        "    options = [2048, 1024, 512];"
        "  } else if (videoCodec === 'h264') {"
        "    options = [4096, 3072, 2048, 1024, 512];"
        "  }"
        "  bitrateSelect.innerHTML = '';"
        "  for (var i = 0; i < options.length; i++) {"
        "    var val = options[i];"
        "    var option = document.createElement('option');"
        "    option.value = val;"
        "    option.text = val + ' kbps';"
        "    if (val == currentMainBitrate) option.selected = true;"
        "    bitrateSelect.appendChild(option);"
        "  }"
        "}"
        "function updateSubBitrateOptions() {"
        "  var videoCodec = document.querySelector('input[name=\"sub_video_codec\"]:checked').value;"
        "  var bitrateSelect = document.getElementById('sub_bitrate');"
        "  var options = [];"
        "  if (videoCodec === 'h265') {"
        "    options = [2048, 1024, 512];"
        "  } else if (videoCodec === 'h264') {"
        "    options = [4096, 3072, 2048, 1024, 512];"
        "  }"
        "  bitrateSelect.innerHTML = '';"
        "  for (var i = 0; i < options.length; i++) {"
        "    var val = options[i];"
        "    var option = document.createElement('option');"
        "    option.value = val;"
        "    option.text = val + ' kbps';"
        "    if (val == currentSubBitrate) option.selected = true;"
        "    bitrateSelect.appendChild(option);"
        "  }"
        "}"
        "function initForm() {"
        "  var mainCodecRadios = document.getElementsByName('main_video_codec');"
        "  for (var i = 0; i < mainCodecRadios.length; i++) {"
        "    if (mainCodecRadios[i].value === currentMainVideoCodec) {"
        "      mainCodecRadios[i].checked = true;"
        "      break;"
        "    }"
        "  }"
        "  updateMainBitrateOptions();"
        "  document.getElementById('main_framerate').value = currentMainFramerate;"
        "  document.getElementById('main_gop').value = currentMainGop;"
        "  var subCodecRadios = document.getElementsByName('sub_video_codec');"
        "  for (var i = 0; i < subCodecRadios.length; i++) {"
        "    if (subCodecRadios[i].value === currentSubVideoCodec) {"
        "      subCodecRadios[i].checked = true;"
        "      break;"
        "    }"
        "  }"
        "  updateSubBitrateOptions();"
        "  document.getElementById('sub_framerate').value = currentSubFramerate;"
        "  document.getElementById('sub_gop').value = currentSubGop;"
        "  var audioCodecRadios = document.getElementsByName('audio_codec');"
        "  for (var i = 0; i < audioCodecRadios.length; i++) {"
        "    if (audioCodecRadios[i].value === currentAudioCodec) {"
        "      audioCodecRadios[i].checked = true;"
        "      break;"
        "    }"
        "  }"
        "  var nightModeRadios = document.getElementsByName('night_mode');"
        "  for (var i = 0; i < nightModeRadios.length; i++) {"
        "    if (nightModeRadios[i].value === currentNightMode) {"
        "      nightModeRadios[i].checked = true;"
        "      break;"
        "    }"
        "  }"
        "  var mainCodecRadios = document.getElementsByName('main_video_codec');"
        "  var updateMainBitrate = function() { updateMainBitrateOptions(); };"
        "  for (var i = 0; i < mainCodecRadios.length; i++) {"
        "    mainCodecRadios[i].addEventListener('change', updateMainBitrate);"
        "  }"
        "  var subCodecRadios = document.getElementsByName('sub_video_codec');"
        "  var updateSubBitrate = function() { updateSubBitrateOptions(); };"
        "  for (var i = 0; i < subCodecRadios.length; i++) {"
        "    subCodecRadios[i].addEventListener('change', updateSubBitrate);"
        "  }"
        "}"
        "function isValidIPv4(ip) {"
        "  if (!ip || ip.length === 0) return false;"
        "  var parts = ip.split('.');"
        "  if (parts.length !== 4) return false;"
        "  for (var i = 0; i < 4; i++) {"
        "    var num = parts[i];"
        "    if (!/^\\d+$/.test(num)) return false;"
        "    var val = parseInt(num, 10);"
        "    if (val < 0 || val > 255) return false;"
        "    if (num !== String(val)) return false;"
        "    if (num.length > 1 && num[0] === '0') return false;"
        "  }"
        "  return true;"
        "}"
        "function validateIpField(fieldId, errorId, fieldName) {"
        "  var field = document.getElementById(fieldId);"
        "  var errorDiv = document.getElementById(errorId);"
        "  var value = field.value.trim();"
        "  if (value.length === 0) {"
        "    errorDiv.textContent = fieldName + '格式错误：必须是有效的IPv4地址（如192.168.1.1），且每段0-255，禁止前导零';"
        "    return false;"
        "  }"
        "  if (!isValidIPv4(value)) {"
        "    errorDiv.textContent = fieldName + '格式错误：必须是有效的IPv4地址（如192.168.1.1），且每段0-255，禁止前导零';"
        "    return false;"
        "  }"
        "  errorDiv.textContent = '';"
        "  return true;"
        "}"
        "function validateStaticIpFields() {"
        "  var mode = document.querySelector('input[name=\"ip_mode\"]:checked').value;"
        "  if (mode !== 'static') return true;"
        "  var valid = true;"
        "  valid = validateIpField('ip_addr', 'ip_addr_error', 'IP地址') && valid;"
        "  valid = validateIpField('gateway', 'gateway_error', '网关') && valid;"
        "  valid = validateIpField('netmask', 'netmask_error', '子网掩码') && valid;"
        "  valid = validateIpField('dns', 'dns_error', 'DNS') && valid;"
        "  return valid;"
        "}"
        "var sdFormatTimeoutId = null;"
        "var sdFormatCheckInterval = null;"
        "function showFormatConfirmDialog() {"
        "  if (confirm('确定要格式化SD卡吗？此操作将删除SD卡上的所有数据！')) {"
        "    startSdFormat();"
        "  }"
        "}"
        "function startSdFormat() {"
        "  var statusSpan = document.getElementById('sdFormatStatus');"
        "  statusSpan.textContent = '格式化中...';"
        "  statusSpan.style.color = '#007bff';"
        "  fetch('/api/sd_format', { method: 'POST' })"
        "    .then(response => response.json())"
        "    .then(data => {"
        "      if (data.status === 'ok') {"
        "        startSdFormatStatusCheck();"
        "      } else {"
        "        statusSpan.textContent = '格式化失败';"
        "        statusSpan.style.color = '#f44336';"
        "        setTimeout(function() { statusSpan.textContent = ''; }, 5000);"
        "      }"
        "    })"
        "    .catch(err => {"
        "      console.error(err);"
        "      statusSpan.textContent = '格式化失败';"
        "      statusSpan.style.color = '#f44336';"
        "      setTimeout(function() { statusSpan.textContent = ''; }, 5000);"
        "    });"
        "}"
        "function startSdFormatStatusCheck() {"
        "  if (sdFormatCheckInterval) {"
        "    clearInterval(sdFormatCheckInterval);"
        "  }"
        "  if (sdFormatTimeoutId) {"
        "    clearTimeout(sdFormatTimeoutId);"
        "  }"
        "  sdFormatTimeoutId = setTimeout(function() {"
        "    clearInterval(sdFormatCheckInterval);"
        "    var statusSpan = document.getElementById('sdFormatStatus');"
        "    if (statusSpan.textContent === '格式化中...') {"
        "      statusSpan.textContent = '格式化超时';"
        "      statusSpan.style.color = '#f44336';"
        "      setTimeout(function() { statusSpan.textContent = ''; }, 5000);"
        "    }"
        "  }, 60000);"
        "  sdFormatCheckInterval = setInterval(checkSdFormatStatus, 1000);"
        "}"
        "function checkSdFormatStatus() {"
        "  fetch('/api/sd_format_status')"
        "    .then(response => response.json())"
        "    .then(data => {"
        "      var statusSpan = document.getElementById('sdFormatStatus');"
        "      if (data.status === 'success') {"
        "        clearInterval(sdFormatCheckInterval);"
        "        clearTimeout(sdFormatTimeoutId);"
        "        statusSpan.textContent = '卡格式化成功';"
        "        statusSpan.style.color = '#4CAF50';"
        "        setTimeout(function() { statusSpan.textContent = ''; }, 5000);"
        "      } else if (data.status === 'failed') {"
        "        clearInterval(sdFormatCheckInterval);"
        "        clearTimeout(sdFormatTimeoutId);"
        "        statusSpan.textContent = '格式化失败';"
        "        statusSpan.style.color = '#f44336';"
        "        setTimeout(function() { statusSpan.textContent = ''; }, 5000);"
        "      }"
        "    })"
        "    .catch(err => console.error(err));"
        "}"
        "</script>"
        "</head><body>"
        "<div class='container'>"
        "<h1>摄像头参数配置</h1>"
        "<p><strong>设备版本号：%s</strong></p>"
        "<form id='configForm'>"
        "<fieldset>"
        "<legend>网络参数</legend>"
        "<div class='radio-group'>"
        "<span>IP模式：</span>"
        "<label><input type='radio' name='ip_mode' value='dhcp' %s onclick='toggleStaticFields()'> 动态IP</label>"
        "<label><input type='radio' name='ip_mode' value='static' %s onclick='toggleStaticFields()'> 静态IP</label>"
        "</div>"
        "<div id='staticFields' style='display:%s'>"
        "<label>IP地址：<input type='text' name='ip_addr' id='ip_addr' value='%s'></label>"
        "<div id='ip_addr_error' class='ip-error'></div>"
        "<label>网关：<input type='text' name='gateway' id='gateway' value='%s'></label>"
        "<div id='gateway_error' class='ip-error'></div>"
        "<label>子网掩码：<input type='text' name='netmask' id='netmask' value='%s'></label>"
        "<div id='netmask_error' class='ip-error'></div>"
        "<label>DNS：<input type='text' name='dns' id='dns' value='%s'></label>"
        "<div id='dns_error' class='ip-error'></div>"
        "</div>"
        "</fieldset>"
        "<fieldset>"
        "<legend>WiFi 配置</legend>"
        "<div class='radio-group'>"
        "<label><input type='checkbox' name='wifi_need_config' value='1' %s> WiFi配网</label>"
        "</div>"
        "<label>SSID：<input type='text' name='wifi_ssid' value='%s' placeholder='请输入WiFi SSID'></label>"
        "<label>密码：<input type='text' name='wifi_password' value='%s' placeholder='请输入WiFi密码'></label>"
        "</fieldset>"
        "<div class='config-row'>"
        "<fieldset>"
        "<legend>国标配置</legend>"
        "<div style='display:flex; align-items:center; margin-bottom:15px;'>"
        "<div class='radio-group'>"
        "<label><input type='checkbox' name='gb_enable' value='1' %s> 启用</label>"
        "</div>"
        "<div id='gb_status' style='padding:10px; border-radius:4px; background:#f5f5f5; margin-left:36px;'>"
        "连接状态：<span id='gb_status_text'>检查中...</span>"
        "</div>"
        "</div>"
        "<label>配置方式：</label>"
        "<select name='gb_config_mode' style='margin-bottom:15px;'>"
        "<option value='0' %s>零配置</option>"
        "<option value='1' %s>手动配置</option>"
        "</select>"
        "<label>SIP服务器编码：<input type='text' name='gb_code' value='%s'></label>"
        "<label>SIP服务器域：<input type='text' name='gb_domain' value='%s'></label>"
        "<label>SIP服务器IP：<input type='text' name='gb_ip' value='%s'></label>"
        "<label>SIP服务器端口：<input type='text' name='gb_port' value='%s'></label>"
        "<label>国标设备编码：<input type='text' name='gb_device_id' value='%s'></label>"
        "<label>国标用户ID：<input type='text' name='gb_user_id' value='%s'></label>"
        "<label>国标接入密码：<input type='text' name='gb_password' value='%s'></label>"
        "<label>视频通道编码1：<input type='text' name='gb_channel_code' value='%s'></label>"
        "</fieldset>"
        "<fieldset>"
        "<legend>1400配置</legend>"
        "<div style='display:flex; align-items:center; margin-bottom:15px;'>"
        "<div class='radio-group'>"
        "<label><input type='checkbox' name='gat1400_enable' value='1' %s> 启用</label>"
        "</div>"
        "<div id='gat1400_status' style='padding:10px; border-radius:4px; background:#f5f5f5; margin-left:36px;'>"
        "连接状态：<span id='gat1400_status_text'>检查中...</span>"
        "</div>"
        "</div>"
        "<label>IPv4接入IP：<input type='text' name='gat1400_ip' value='%s'></label>"
        "<label>IPv4接入端口：<input type='text' name='gat1400_port' value='%s'></label>"
        "<label>IPv6接入地址：<input type='text' name='gat1400_ipv6' value='%s'></label>"
        "<label>IPv6接入端口：<input type='text' name='gat1400_ipv6_port' value='%s'></label>"
        "<label>设备用户：<input type='text' name='gat1400_user' value='%s'></label>"
        "<label>设备编码：<input type='text' name='gat1400_device_id' value='%s'></label>"
        "<label>设备密码：<input type='text' name='gat1400_password' value='%s'></label>"
        "</fieldset>"
        "</div>"
        "<fieldset>"
        "<legend>视频参数</legend>"
        "<div class='config-row'>"
        "<fieldset>"
        "<legend>主码流 (2560x1440)</legend>"
        "<div class='radio-group'>"
        "<span>视频编码：</span>"
        "<input type='radio' id='main_video_codec_h265' name='main_video_codec' value='h265'><label for='main_video_codec_h265'>H265</label>"
        "<input type='radio' id='main_video_codec_h264' name='main_video_codec' value='h264'><label for='main_video_codec_h264'>H264</label>"
        "</div>"
        "<label>码率：</label>"
        "<select id='main_bitrate' name='main_bitrate'></select>"
        "<label>帧率：</label>"
        "<input type='number' name='main_framerate' id='main_framerate' step='1' min='15' max='30'>"
        "<label>关键帧间隔(GOP)：</label>"
        "<input type='number' name='main_gop' id='main_gop' step='1' min='30' max='70'>"
        "</fieldset>"
        "<fieldset>"
        "<legend>子码流 (1280x720)</legend>"
        "<div class='radio-group'>"
        "<span>视频编码：</span>"
        "<input type='radio' id='sub_video_codec_h265' name='sub_video_codec' value='h265'><label for='sub_video_codec_h265'>H265</label>"
        "<input type='radio' id='sub_video_codec_h264' name='sub_video_codec' value='h264'><label for='sub_video_codec_h264'>H264</label>"
        "</div>"
        "<label>码率：</label>"
        "<select id='sub_bitrate' name='sub_bitrate'></select>"
        "<label>帧率：</label>"
        "<input type='number' name='sub_framerate' id='sub_framerate' step='1' min='15' max='30'>"
        "<label>关键帧间隔(GOP)：</label>"
        "<input type='number' name='sub_gop' id='sub_gop' step='1' min='30' max='70'>"
        "</fieldset>"
        "</div>"
        "</fieldset>"
        "<fieldset>"
        "<legend>音频参数</legend>"
        "<div class='radio-group'>"
        "<span>音频编码：</span>"
        "<input type='radio' id='audio_codec_g711a' name='audio_codec' value='g711a'><label for='audio_codec_g711a'>G711A</label>"
        "<input type='radio' id='audio_codec_g711u' name='audio_codec' value='g711u'><label for='audio_codec_g711u'>G711U</label>"
        "<input type='radio' id='audio_codec_aac' name='audio_codec' value='aac'><label for='audio_codec_aac'>AAC</label>"
        "</div>"
        "</fieldset>"
        "<fieldset>"
        "<legend>夜视功能</legend>"
        "<div class='radio-group'>"
        "<span>夜视模式：</span>"
        "<input type='radio' id='night_mode_default' name='night_mode' value='default'><label for='night_mode_default'>默认</label>"
        "<input type='radio' id='night_mode_white_light' name='night_mode' value='white_light'><label for='night_mode_white_light'>白光灯开启</label>"
        "<input type='radio' id='night_mode_infrared' name='night_mode' value='infrared'><label for='night_mode_infrared'>红外灯开启</label>"
        "</div>"
        "</fieldset>"
        "<fieldset>"
        "<legend>SD卡相关</legend>"
        "<div style='display:flex; align-items:center; gap:10px;'>"
        "<button type='button' id='sdFormatBtn' onclick='showFormatConfirmDialog()'>SD卡格式化</button>"
        "<span id='sdFormatStatus'></span>"
        "</div>"
        "</fieldset>"
        "<div class='button-row'>"
        "<button type='submit' class='save-btn'>保存</button>"
        "<button type='button' class='logout-btn' onclick='location.href=\"/logout\"'>退出</button>"
        "</div>"
        "<div id='message' class='message'></div>"
        "</form>"
        "</div>"
        "<script>"
        "initForm();"
        "document.getElementById('configForm').addEventListener('submit', function(e){"
        "  e.preventDefault();"
        "  if (!validateStaticIpFields()) { return; }"
        "  var formData = new FormData(this);"
        "  fetch('/config', { method: 'POST', body: new URLSearchParams(formData) })"
        "    .then(response => response.json())"
        "    .then(data => {"
        "      var msgDiv = document.getElementById('message');"
        "      if(data.status === 'ok'){"
        "        msgDiv.textContent = '保存成功';"
        "        msgDiv.className = 'message success';"
        "      } else {"
        "        msgDiv.textContent = '保存失败，错误码 ' + (data.code || '');"
        "        msgDiv.className = 'message error';"
        "      }"
        "      setTimeout(function(){ msgDiv.textContent = ''; msgDiv.className = 'message'; }, 2000);"
        "    })"
        "    .catch(function(){"
        "      var msgDiv = document.getElementById('message');"
        "      msgDiv.textContent = '保存失败';"
        "      msgDiv.className = 'message error';"
        "      setTimeout(function(){ msgDiv.textContent = ''; msgDiv.className = 'message'; }, 2000);"
        "    });"
        "});"
        "document.getElementById('ip_addr').addEventListener('input', function(){ validateIpField('ip_addr', 'ip_addr_error', 'IP地址'); });"
        "document.getElementById('gateway').addEventListener('input', function(){ validateIpField('gateway', 'gateway_error', '网关'); });"
        "document.getElementById('netmask').addEventListener('input', function(){ validateIpField('netmask', 'netmask_error', '子网掩码'); });"
        "document.getElementById('dns').addEventListener('input', function(){ validateIpField('dns', 'dns_error', 'DNS'); });"
        "function updateGbStatus() {"
        "  fetch('/api/gb_status')"
        "    .then(response => response.json())"
        "    .then(data => {"
        "      var statusDiv = document.getElementById('gb_status');"
        "      var statusText = document.getElementById('gb_status_text');"
        "      if (data.status === 'online') {"
        "        statusText.textContent = '在线';"
        "        statusDiv.style.background = '#d4edda';"
        "        statusDiv.style.color = '#155724';"
        "      } else {"
        "        statusText.textContent = '离线';"
        "        statusDiv.style.background = '#f8d7da';"
        "        statusDiv.style.color = '#721c24';"
        "      }"
        "    })"
        "    .catch(err => {"
        "      var statusDiv = document.getElementById('gb_status');"
        "      var statusText = document.getElementById('gb_status_text');"
        "      statusText.textContent = '获取失败';"
        "      statusDiv.style.background = '#f8d7da';"
        "      statusDiv.style.color = '#721c24';"
        "    });"
        "}"
        "function updateGat1400Status() {"
        "  fetch('/api/gat1400_status')"
        "    .then(response => response.json())"
        "    .then(data => {"
        "      var statusDiv = document.getElementById('gat1400_status');"
        "      var statusText = document.getElementById('gat1400_status_text');"
        "      if (data.status === 'online') {"
        "        statusText.textContent = '在线';"
        "        statusDiv.style.background = '#d4edda';"
        "        statusDiv.style.color = '#155724';"
        "      } else {"
        "        statusText.textContent = '离线';"
        "        statusDiv.style.background = '#f8d7da';"
        "        statusDiv.style.color = '#721c24';"
        "      }"
        "    })"
        "    .catch(err => {"
        "      var statusDiv = document.getElementById('gat1400_status');"
        "      var statusText = document.getElementById('gat1400_status_text');"
        "      statusText.textContent = '获取失败';"
        "      statusDiv.style.background = '#f8d7da';"
        "      statusDiv.style.color = '#721c24';"
        "    });"
        "}"
        "setInterval(updateGbStatus, 3000);"
        "setInterval(updateGat1400Status, 3000);"
        "updateGbStatus();"
        "updateGat1400Status();"
        "</script>"
        "</body></html>";

    const char *dhcp_checked = (state->ip_mode == 0) ? "checked" : "";
    const char *static_checked = (state->ip_mode == 1) ? "checked" : "";
    const char *static_display = (state->ip_mode == 1) ? "block" : "none";

    // 计算所需缓冲区大小
    size_t len = strlen(fmt) + strlen(state->version) + strlen(dhcp_checked) + strlen(static_checked) +
                 strlen(static_display) + strlen(state->ip_addr) + strlen(state->gateway) +
                 strlen(state->netmask) + strlen(state->dns) +
                 strlen(wifi_checked) + strlen(state->wifi_ssid) + strlen(state->wifi_password) +
                 strlen(state->gb_code) + strlen(state->gb_ip) + strlen(state->gb_port) +
                 strlen(state->gb_domain) + strlen(state->gb_user_id) + strlen(state->gb_channel_code) +
                 strlen(state->gb_device_id) + strlen(state->gb_password) +
                 strlen(main_codec_str) + strlen(sub_codec_str) +
                 strlen(audio_codec_str) + strlen(night_mode_str) +
                 strlen(state->gat1400_ip) + strlen(state->gat1400_port) +
                 strlen(state->gat1400_ipv6) + strlen(state->gat1400_ipv6_port) +
                 strlen(state->gat1400_user) +
                 strlen(state->gat1400_device_id) + strlen(state->gat1400_password) + 512;
    char *html = malloc(len);
    if (!html) return NULL;
    const char *gb_config_mode_0_selected = (state->gb_config_mode == 0) ? "selected" : "";
    const char *gb_config_mode_1_selected = (state->gb_config_mode == 1) ? "selected" : "";
    const char *gat1400_enable_checked = (state->gat1400_enable == 1) ? "checked" : "";
    snprintf(html, len, fmt,
             main_codec_str, main_bitrate, main_framerate, main_gop,
             sub_codec_str, sub_bitrate, sub_framerate, sub_gop,
             audio_codec_str, night_mode_str,
             state->version,
             dhcp_checked, static_checked, static_display,
             state->ip_addr, state->gateway, state->netmask, state->dns,
             wifi_checked, state->wifi_ssid, state->wifi_password,
             gb_enable_checked,
             gb_config_mode_0_selected, gb_config_mode_1_selected,
             state->gb_code, state->gb_domain, state->gb_ip, state->gb_port,
             state->gb_device_id, state->gb_user_id, state->gb_password, state->gb_channel_code,
             gat1400_enable_checked,
             state->gat1400_ip, state->gat1400_port,
             state->gat1400_ipv6, state->gat1400_ipv6_port,
             state->gat1400_user,
             state->gat1400_device_id, state->gat1400_password);
    return html;
}

static const char* get_success_json(void) {
    return "{\"status\":\"ok\"}";
}

// ================== Session 管理 ==================
static void generate_session_id(char *buf, size_t len) {
    static int seeded = 0;
    if (!seeded) {
        srand(time(NULL));
        seeded = 1;
    }
    const char *chars = "0123456789abcdefghijklmnopqrstuvwxyz";
    for (size_t i = 0; i < len - 1; i++) {
        buf[i] = chars[rand() % (strlen(chars))];
    }
    buf[len - 1] = '\0';
}

static const char* create_session(void) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!sessions[i].valid) {
            generate_session_id(sessions[i].id, sizeof(sessions[i].id));
            sessions[i].valid = 1;
            session_count++;
            return sessions[i].id;
        }
    }
    for (int i = 0; i < MAX_SESSIONS; i++) sessions[i].valid = 0;
    session_count = 0;
    return create_session();
}

static int check_session(const char *id) {
    if (!id) return 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].valid && strcmp(sessions[i].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

static void delete_session(const char *id) {
    if (!id) return;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].valid && strcmp(sessions[i].id, id) == 0) {
            sessions[i].valid = 0;
            session_count--;
            break;
        }
    }
}

static char* get_session_from_cookie(struct mg_http_message *hm) {
    struct mg_str *cookie = mg_http_get_header(hm, "Cookie");
    if (!cookie || cookie->len == 0) return NULL;
    char *buf = malloc(cookie->len + 1);
    memcpy(buf, cookie->buf, cookie->len);
    buf[cookie->len] = '\0';
    const char *start = strstr(buf, "session_id=");
    if (!start) {
        free(buf);
        return NULL;
    }
    start += strlen("session_id=");
    const char *end = strchr(start, ';');
    if (!end) end = start + strlen(start);
    int len = end - start;
    char *id = malloc(len + 1);
    memcpy(id, start, len);
    id[len] = '\0';
    free(buf);
    return id;
}

// ================== POST 数据解析 ==================
static int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static void UrlDecodeInPlace(char *text) {
    if (text == NULL) return;

    char *src = text;
    char *dst = text;
    while (*src != '\0') {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            int high = HexValue(src[1]);
            int low = HexValue(src[2]);
            *dst++ = (char)((high << 4) | low);
            src += 3;
            continue;
        }
        if (*src == '+') {
            *dst++ = ' ';
            ++src;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

static void CopyDecodedFormValue(char *dst, size_t dst_size, char *value) {
    if (dst == NULL || dst_size == 0 || value == NULL) return;

    UrlDecodeInPlace(value);
    strncpy(dst, value, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void parse_form_data(struct mg_str body, device_state_t *state) {
    char *buf = malloc(body.len + 1);
    memcpy(buf, body.buf, body.len);
    buf[body.len] = '\0';

    if (strstr(buf, "ip_mode=dhcp")) state->ip_mode = 0;
    else if (strstr(buf, "ip_mode=static")) state->ip_mode = 1;

    char *val;
    if ((val = strstr(buf, "ip_addr="))) {
        val += strlen("ip_addr=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->ip_addr, val, sizeof(state->ip_addr)-1);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gateway="))) {
        val += strlen("gateway=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->gateway, val, sizeof(state->gateway)-1);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "netmask="))) {
        val += strlen("netmask=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->netmask, val, sizeof(state->netmask)-1);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "dns="))) {
        val += strlen("dns=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->dns, val, sizeof(state->dns)-1);
        if (end) *end = '&';
    }

    if ((val = strstr(buf, "gb_code="))) {
        val += strlen("gb_code=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->gb_code, val, sizeof(state->gb_code)-1);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gb_domain="))) {
        val += strlen("gb_domain=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->gb_domain, val, sizeof(state->gb_domain)-1);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gb_ip="))) {
        val += strlen("gb_ip=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->gb_ip, val, sizeof(state->gb_ip)-1);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gb_port="))) {
        val += strlen("gb_port=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->gb_port, val, sizeof(state->gb_port)-1);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gb_device_id="))) {
        val += strlen("gb_device_id=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->gb_device_id, val, sizeof(state->gb_device_id)-1);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gb_user_id="))) {
        val += strlen("gb_user_id=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->gb_user_id, val, sizeof(state->gb_user_id)-1);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gb_password="))) {
        val += strlen("gb_password=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->gb_password, val, sizeof(state->gb_password)-1);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gb_channel_code="))) {
        val += strlen("gb_channel_code=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->gb_channel_code, val, sizeof(state->gb_channel_code)-1);
        if (end) *end = '&';
    }

    // 解析主码流视频参数
    if ((val = strstr(buf, "main_video_codec="))) {
        val += strlen("main_video_codec=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        state->main_stream.video_codec = str_to_codec(val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "main_bitrate="))) {
        val += strlen("main_bitrate=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        state->main_stream.bitrate = atoi(val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "main_framerate="))) {
        val += strlen("main_framerate=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        state->main_stream.framerate = atoi(val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "main_gop="))) {
        val += strlen("main_gop=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        state->main_stream.gop = atoi(val);
        if (end) *end = '&';
    }
    // 解析子码流视频参数
    if ((val = strstr(buf, "sub_video_codec="))) {
        val += strlen("sub_video_codec=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        state->sub_stream.video_codec = str_to_codec(val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "sub_bitrate="))) {
        val += strlen("sub_bitrate=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        state->sub_stream.bitrate = atoi(val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "sub_framerate="))) {
        val += strlen("sub_framerate=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        state->sub_stream.framerate = atoi(val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "sub_gop="))) {
        val += strlen("sub_gop=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        state->sub_stream.gop = atoi(val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "audio_codec="))) {
        val += strlen("audio_codec=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        state->audio_codec = str_to_audio_codec(val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "night_mode="))) {
        val += strlen("night_mode=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        state->night_mode = str_to_night_mode(val);
        if (end) *end = '&';
    }

    // 解析1400配置
    if ((val = strstr(buf, "gat1400_ip="))) {
        val += strlen("gat1400_ip=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        CopyDecodedFormValue(state->gat1400_ip, sizeof(state->gat1400_ip), val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gat1400_port="))) {
        val += strlen("gat1400_port=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        CopyDecodedFormValue(state->gat1400_port, sizeof(state->gat1400_port), val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gat1400_ipv6="))) {
        val += strlen("gat1400_ipv6=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        CopyDecodedFormValue(state->gat1400_ipv6, sizeof(state->gat1400_ipv6), val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gat1400_ipv6_port="))) {
        val += strlen("gat1400_ipv6_port=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        CopyDecodedFormValue(state->gat1400_ipv6_port, sizeof(state->gat1400_ipv6_port), val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gat1400_user="))) {
        val += strlen("gat1400_user=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        CopyDecodedFormValue(state->gat1400_user, sizeof(state->gat1400_user), val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gat1400_device_id="))) {
        val += strlen("gat1400_device_id=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        CopyDecodedFormValue(state->gat1400_device_id, sizeof(state->gat1400_device_id), val);
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "gat1400_password="))) {
        val += strlen("gat1400_password=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        CopyDecodedFormValue(state->gat1400_password, sizeof(state->gat1400_password), val);
        if (end) *end = '&';
    }

    // 解析1400配置启用状态
    state->gat1400_enable = 0;
    if ((val = strstr(buf, "gat1400_enable="))) {
        state->gat1400_enable = 1;
    }

    // 解析国标配置
    state->gb_enable = 0;
    if ((val = strstr(buf, "gb_enable="))) {
        state->gb_enable = 1;
    }
    if ((val = strstr(buf, "gb_config_mode="))) {
        val += strlen("gb_config_mode=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        state->gb_config_mode = atoi(val);
        if (end) *end = '&';
    }

    // 解析 WiFi 配置
    state->wifi_need_config = 0;
    if ((val = strstr(buf, "wifi_need_config="))) {
        state->wifi_need_config = 1;
    }
    if ((val = strstr(buf, "wifi_ssid="))) {
        val += strlen("wifi_ssid=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->wifi_ssid, val, sizeof(state->wifi_ssid)-1);
        state->wifi_ssid[sizeof(state->wifi_ssid)-1] = '\0';
        if (end) *end = '&';
    }
    if ((val = strstr(buf, "wifi_password="))) {
        val += strlen("wifi_password=");
        char *end = strchr(val, '&');
        if (end) *end = '\0';
        strncpy(state->wifi_password, val, sizeof(state->wifi_password)-1);
        state->wifi_password[sizeof(state->wifi_password)-1] = '\0';
        if (end) *end = '&';
    }

    free(buf);
}

// ================== 对外接口 ==================
void set_device_state(const device_state_t *state) {
    if (state) {
        memcpy(&current_state, state, sizeof(device_state_t));
        current_state.version[sizeof(current_state.version)-1] = '\0';
        current_state.ip_addr[sizeof(current_state.ip_addr)-1] = '\0';
        current_state.gateway[sizeof(current_state.gateway)-1] = '\0';
        current_state.netmask[sizeof(current_state.netmask)-1] = '\0';
        current_state.dns[sizeof(current_state.dns)-1] = '\0';
        current_state.gb_code[sizeof(current_state.gb_code)-1] = '\0';
        current_state.gb_domain[sizeof(current_state.gb_domain)-1] = '\0';
        current_state.gb_ip[sizeof(current_state.gb_ip)-1] = '\0';
        current_state.gb_port[sizeof(current_state.gb_port)-1] = '\0';
        current_state.gb_device_id[sizeof(current_state.gb_device_id)-1] = '\0';
        current_state.gb_user_id[sizeof(current_state.gb_user_id)-1] = '\0';
        current_state.gb_password[sizeof(current_state.gb_password)-1] = '\0';
        current_state.gb_channel_code[sizeof(current_state.gb_channel_code)-1] = '\0';
        current_state.gat1400_ip[sizeof(current_state.gat1400_ip)-1] = '\0';
        current_state.gat1400_port[sizeof(current_state.gat1400_port)-1] = '\0';
        current_state.gat1400_ipv6[sizeof(current_state.gat1400_ipv6)-1] = '\0';
        current_state.gat1400_ipv6_port[sizeof(current_state.gat1400_ipv6_port)-1] = '\0';
        current_state.gat1400_user[sizeof(current_state.gat1400_user)-1] = '\0';
        current_state.gat1400_device_id[sizeof(current_state.gat1400_device_id)-1] = '\0';
        current_state.gat1400_password[sizeof(current_state.gat1400_password)-1] = '\0';
        current_state.wifi_ssid[sizeof(current_state.wifi_ssid)-1] = '\0';
        current_state.wifi_password[sizeof(current_state.wifi_password)-1] = '\0';
    }
}

static void print_config_header(void) { printf("\n========== Config Saved ==========\n"); }
static void print_basic_config(const device_state_t *state) { printf("Version: %s\n", state->version); }
static void print_ip_config(const device_state_t *state) {
    printf("IP Mode: %s\n", state->ip_mode == 0 ? "DHCP" : "Static");
    if (state->ip_mode == 1) {
        printf("Static IP: %s\n", state->ip_addr);
        printf("Gateway: %s\n", state->gateway);
        printf("Netmask: %s\n", state->netmask);
        printf("DNS: %s\n", state->dns);
    }
}
static void print_gb_config(const device_state_t *state) {
    printf("GB Config:\n");
    printf("  Need Config: %s\n", state->gb_enable ? "YES" : "NO");
    printf("  Config Mode: %s\n", state->gb_config_mode == 0 ? "Zero Config" : "Manual Config");
    if(state->gb_enable) {
        printf("  Code: %s\n", state->gb_code);
        printf("  Domain: %s\n", state->gb_domain);
        printf("  IP: %s\n", state->gb_ip);
        printf("  Port: %s\n", state->gb_port);
        printf("  Device ID: %s\n", state->gb_device_id);
        printf("  User ID: %s\n", state->gb_user_id);
        printf("  Password: %s\n", state->gb_password);
        printf("  Channel Code: %s\n", state->gb_channel_code);
    }  
}
static void print_video_config(const device_state_t *state) {
    const char *main_codec_str = (state->main_stream.video_codec == VIDEO_CODEC_H265) ? "H265" : "H264";
    const char *sub_codec_str = (state->sub_stream.video_codec == VIDEO_CODEC_H265) ? "H265" : "H264";
    printf("Video Config:\n");
    printf("  Main Stream (2560x1440):\n");
    printf("    Codec: %s\n", main_codec_str);
    printf("    Bitrate: %d kbps\n", state->main_stream.bitrate);
    printf("    Framerate: %d fps\n", state->main_stream.framerate);
    printf("    GOP: %d\n", state->main_stream.gop);
    printf("  Sub Stream (1280x720):\n");
    printf("    Codec: %s\n", sub_codec_str);
    printf("    Bitrate: %d kbps\n", state->sub_stream.bitrate);
    printf("    Framerate: %d fps\n", state->sub_stream.framerate);
    printf("    GOP: %d\n", state->sub_stream.gop);
}
static void print_audio_config(const device_state_t *state) {
    const char *audio_str = "G711A";
    if (state->audio_codec == AUDIO_CODEC_G711U) audio_str = "G711U";
    else if (state->audio_codec == AUDIO_CODEC_AAC) audio_str = "AAC";
    printf("Audio Config:\n");
    printf("  Audio Codec: %s\n", audio_str);
}
static void print_night_mode_config(const device_state_t *state) {
    const char *night_str = "默认";
    if (state->night_mode == NIGHT_MODE_WHITE_LIGHT) night_str = "白光灯开启";
    else if (state->night_mode == NIGHT_MODE_INFRARED) night_str = "红外灯开启";
    printf("Night Mode Config:\n");
    printf("  Night Mode: %s\n", night_str);
}
static void print_1400_config(const device_state_t *state) {
    printf("1400 Config:\n");
    printf("  Need Config: %s\n", state->gat1400_enable ? "YES" : "NO");
    printf("  IP: %s\n", state->gat1400_ip);
    printf("  Port: %s\n", state->gat1400_port);
    printf("  IPv6: %s\n", state->gat1400_ipv6);
    printf("  IPv6 Port: %s\n", state->gat1400_ipv6_port);
    printf("  User: %s\n", state->gat1400_user);
    printf("  Device ID: %s\n", state->gat1400_device_id);
    printf("  Password: %s\n", state->gat1400_password);
}
static void print_wifi_config(const device_state_t *state) {
    printf("WiFi Config:\n");
    printf("  Need Config: %s\n", state->wifi_need_config ? "是" : "否");
    if (state->wifi_need_config) {
        printf("  SSID: %s\n", state->wifi_ssid);
        printf("  Password: %s\n", state->wifi_password);
    }
}
static void print_config_footer(void) { printf("==================================\n"); }

static config_update_callback_t user_callback = NULL;

void web_server_register_config_callback(config_update_callback_t callback) {
    user_callback = callback;
}

static sd_format_callback_t sd_format_callback = NULL;

void web_server_register_sd_format_callback(sd_format_callback_t callback) {
    sd_format_callback = callback;
}

/**
 * @brief 更新国标服务器连接状态
 * @param status 新的连接状态（在线/离线）
 */
void update_gb_status(gb_status_t status) {
    current_state.gb_status = status;
    printf("[Web] GB status updated to: %s", status == GB_STATUS_ONLINE ? "Online" : "Offline");
}

/**
 * @brief 更新1400服务器连接状态
 * @param status 新的连接状态（在线/离线）
 */
void update_gat1400_status(gat1400_status_t status) {
    current_state.gat1400_status = status;
    printf("[Web] 1400 status updated to: %s", status == GAT1400_STATUS_ONLINE ? "Online" : "Offline");
}

void update_sd_format_status(sd_format_status_t status) {
    current_state.sd_format_status = status;
    const char *status_str = "";
    switch (status) {
        case SD_FORMAT_IDLE: status_str = "Idle"; break;
        case SD_FORMAT_FORMATTING: status_str = "Formatting"; break;
        case SD_FORMAT_SUCCESS: status_str = "Success"; break;
        case SD_FORMAT_FAILED: status_str = "Failed"; break;
        case SD_FORMAT_TIMEOUT: status_str = "Timeout"; break;
    }
    printf("[Web] SD format status updated to: %s\n", status_str);
}

static int handle_config_update(const device_state_t *state) {
    int ret = 0;
    if (user_callback) {
        print_config_header();
        print_basic_config(state);
        print_ip_config(state);
        print_gb_config(state);
        print_video_config(state);
        print_audio_config(state);
        print_night_mode_config(state);
        print_1400_config(state);
        print_wifi_config(state);
        print_config_footer();
        ret = user_callback(state);
        if (ret != 0) {
            printf("[Error] Config update failed with error code: %d\n", ret);
        }
    } else {
        print_config_header();
        print_basic_config(state);
        print_ip_config(state);
        print_gb_config(state);
        print_video_config(state);
        print_audio_config(state);
        print_night_mode_config(state);
        print_1400_config(state);
        print_wifi_config(state);
        print_config_footer();
        ret = 0;
    }
    return ret;
}

// ================== HTTP 处理 ==================
static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        int is_get = mg_strcmp(hm->method, mg_str("GET")) == 0;
        int is_post = mg_strcmp(hm->method, mg_str("POST")) == 0;

        if (mg_strcmp(hm->uri, mg_str("/login")) == 0) {
            if (is_get) {
                mg_http_reply(c, 200, "Content-Type: text/html; charset=utf-8\r\n", "%s", get_login_page());
            } else if (is_post) {
                char username[32] = {0}, password[32] = {0};
                char *buf = malloc(hm->body.len + 1);
                memcpy(buf, hm->body.buf, hm->body.len);
                buf[hm->body.len] = '\0';
                char *token = strtok(buf, "&");
                while (token) {
                    char *eq = strchr(token, '=');
                    if (eq) {
                        *eq = '\0';
                        char *key = token;
                        char *val = eq + 1;
                        if (strcmp(key, "username") == 0) strncpy(username, val, sizeof(username)-1);
                        else if (strcmp(key, "password") == 0) strncpy(password, val, sizeof(password)-1);
                    }
                    token = strtok(NULL, "&");
                }
                free(buf);
                if (strcmp(username, g_login_username) == 0 && strcmp(password, g_login_password) == 0) {
                    const char *sid = create_session();
                    char header[256];
                    snprintf(header, sizeof(header), "Set-Cookie: session_id=%s; Path=/\r\nLocation: /\r\n", sid);
                    mg_http_reply(c, 302, header, "");
                } else {
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"error\":\"账号或者密码错误，请重新输入\"}");
                }
            }
        }
        else if (mg_strcmp(hm->uri, mg_str("/logout")) == 0) {
            char *sid = get_session_from_cookie(hm);
            if (sid) {
                delete_session(sid);
                free(sid);
            }
            mg_http_reply(c, 302, "Set-Cookie: session_id=; Path=/; Max-Age=0\r\nLocation: /login\r\n", "");
        }
        else if (mg_strcmp(hm->uri, mg_str("/")) == 0 || mg_strcmp(hm->uri, mg_str("/config_page")) == 0) {
            char *sid = get_session_from_cookie(hm);
            int authed = (sid && check_session(sid));
            free(sid);
            if (!authed) {
                mg_http_reply(c, 302, "Location: /login\r\n", "");
                return;
            }

            if (is_get) {
                char *html = get_config_page(&current_state);
                if (html) {
                    mg_http_reply(c, 200, "Content-Type: text/html; charset=utf-8\r\n", "%s", html);
                    free(html);
                } else {
                    mg_http_reply(c, 500, "", "Internal server error");
                }
            }
        }
        else if (mg_strcmp(hm->uri, mg_str("/config")) == 0 && is_post) {
            char *sid = get_session_from_cookie(hm);
            int authed = (sid && check_session(sid));
            free(sid);
            if (!authed) {
                mg_http_reply(c, 401, "Content-Type: application/json\r\n", "{\"status\":\"unauthorized\"}");
                return;
            }

            device_state_t new_state = current_state;
            parse_form_data(hm->body, &new_state);
            current_state = new_state;
            int ret = handle_config_update(&current_state);
            if (ret == 0) {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
            } else {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"error\", \"code\":%d}", ret);
            }
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/gb_status")) == 0 && is_get) {
            char *sid = get_session_from_cookie(hm);
            int authed = (sid && check_session(sid));
            free(sid);
            if (!authed) {
                mg_http_reply(c, 401, "Content-Type: application/json\r\n", "{\"status\":\"unauthorized\"}");
                return;
            }
            const char *status_str = (current_state.gb_status == GB_STATUS_ONLINE) ? "online" : "offline";
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"%s\"}", status_str);
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/gat1400_status")) == 0 && is_get) {
            char *sid = get_session_from_cookie(hm);
            int authed = (sid && check_session(sid));
            free(sid);
            if (!authed) {
                mg_http_reply(c, 401, "Content-Type: application/json\r\n", "{\"status\":\"unauthorized\"}");
                return;
            }
            const char *status_str = (current_state.gat1400_status == GAT1400_STATUS_ONLINE) ? "online" : "offline";
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"%s\"}", status_str);
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/sd_format_status")) == 0 && is_get) {
            char *sid = get_session_from_cookie(hm);
            int authed = (sid && check_session(sid));
            free(sid);
            if (!authed) {
                mg_http_reply(c, 401, "Content-Type: application/json\r\n", "{\"status\":\"unauthorized\"}");
                return;
            }
            const char *status_str = "";
            switch (current_state.sd_format_status) {
                case SD_FORMAT_IDLE: status_str = "idle"; break;
                case SD_FORMAT_FORMATTING: status_str = "formatting"; break;
                case SD_FORMAT_SUCCESS: status_str = "success"; break;
                case SD_FORMAT_FAILED: status_str = "failed"; break;
                case SD_FORMAT_TIMEOUT: status_str = "timeout"; break;
            }
            mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"%s\"}", status_str);
        }
        else if (mg_strcmp(hm->uri, mg_str("/api/sd_format")) == 0 && is_post) {
            char *sid = get_session_from_cookie(hm);
            int authed = (sid && check_session(sid));
            free(sid);
            if (!authed) {
                mg_http_reply(c, 401, "Content-Type: application/json\r\n", "{\"status\":\"unauthorized\"}");
                return;
            }
            if (sd_format_callback) {
                int ret = sd_format_callback();
                if (ret == 0) {
                    current_state.sd_format_status = SD_FORMAT_FORMATTING;
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"ok\"}");
                } else {
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"error\", \"code\":%d}", ret);
                }
            } else {
                mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"status\":\"error\", \"code\":-1}");
            }
        }
        else {
            mg_http_reply(c, 404, "", "Not Found");
        }
    }
}

// ================== Web 服务主循环 ==================
int start_web_server(void) {
    if (web_running) {
        printf("[Web] Server already running\n");
        return -1;
    }
    mg_log_set(MG_LL_ERROR);//设置日志级别为错误
    mg_mgr_init(&mgr);
    if (mg_http_listen(&mgr, "http://0.0.0.0:80", fn, NULL) == NULL) {
        printf("[Web] Failed to listen on port 80\n");
        mg_mgr_free(&mgr);
        return -1;
    }
    printf("[Web] Server started on http://localhost:80\n");
    web_running = 1;
    web_stop_requested = 0;   // 重置停止标志

    while (web_running) {
        mg_mgr_poll(&mgr, 1000);
        if (web_stop_requested) {
            // 再调用一次 mg_mgr_poll，确保所有待发送数据已发送
            mg_mgr_poll(&mgr, 0);
            web_running = 0;
        }
    }

    mg_mgr_free(&mgr);
    printf("[Web] Server stopped\n");
    return 0;
}

void stop_web_server(void) {
    if (!web_running) {
        printf("[Web] Server not running\n");
        return;
    }
    printf("[Web] Stopping server...\n");
    web_stop_requested = 1;   // 仅设置标志，不立即退出
}
