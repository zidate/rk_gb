#include "DmLwm2mObjects.h"

#include "DmCrypto.h"

#include <stdio.h>
#include <string.h>

#include <map>
#include <sstream>
#include <vector>

namespace dm
{
namespace
{

enum
{
    kDmObjectConfig = 668,
    kDmObjectInfo = 669,
    kDmResourceFieldConfig = 1,
    kDmResourceRuleConfig = 2,
    kDmResourceAddressConfig = 3,
    kDmResourceDeviceInfo = 1
};

struct DmInstance
{
    DmInstance* next;
    uint16_t id;
};

std::string JsonEscape(const std::string& input)
{
    std::ostringstream out;
    for (size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        switch (ch) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
                break;
        }
    }
    return out.str();
}

std::string DataToString(const lwm2m_data_t& data)
{
    switch (data.type) {
        case LWM2M_TYPE_STRING:
        case LWM2M_TYPE_OPAQUE:
        case LWM2M_TYPE_CORE_LINK:
            if (data.value.asBuffer.buffer == NULL || data.value.asBuffer.length == 0) {
                return std::string();
            }
            return std::string(reinterpret_cast<const char*>(data.value.asBuffer.buffer),
                               data.value.asBuffer.length);
        case LWM2M_TYPE_INTEGER: {
            char buffer[32] = {0};
            snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(data.value.asInteger));
            return buffer;
        }
        case LWM2M_TYPE_UNSIGNED_INTEGER: {
            char buffer[32] = {0};
            snprintf(buffer, sizeof(buffer), "%llu",
                     static_cast<unsigned long long>(data.value.asUnsigned));
            return buffer;
        }
        case LWM2M_TYPE_BOOLEAN:
            return data.value.asBoolean ? "Y" : "N";
        default:
            return std::string();
    }
}

bool FieldEnabled(const std::string& fieldConfig, const std::string& field)
{
    if (fieldConfig.empty()) {
        return true;
    }

    const std::string quoted = "\"" + field + "\"";
    size_t pos = fieldConfig.find(quoted);
    if (pos == std::string::npos) {
        return false;
    }

    pos = fieldConfig.find(':', pos + quoted.size());
    if (pos == std::string::npos) {
        return false;
    }

    size_t value = fieldConfig.find_first_not_of(" \t\r\n\"", pos + 1);
    if (value == std::string::npos) {
        return false;
    }
    return fieldConfig[value] == 'Y' || fieldConfig[value] == 'y';
}

int JsonIntValue(const std::string& json, const std::string& field, int fallback)
{
    const std::string quoted = "\"" + field + "\"";
    size_t pos = json.find(quoted);
    if (pos == std::string::npos) {
        return fallback;
    }

    pos = json.find(':', pos + quoted.size());
    if (pos == std::string::npos) {
        return fallback;
    }

    size_t begin = json.find_first_of("-0123456789", pos + 1);
    if (begin == std::string::npos) {
        return fallback;
    }
    return atoi(json.c_str() + begin);
}

void AddStringResource(lwm2m_data_t* data, uint16_t id, const std::string& value)
{
    data->id = id;
    lwm2m_data_encode_string(value.c_str(), data);
}

lwm2m_object_t* AllocateObject(uint16_t objectId, DmObjectState* state)
{
    lwm2m_object_t* objectP = static_cast<lwm2m_object_t*>(lwm2m_malloc(sizeof(lwm2m_object_t)));
    if (objectP == NULL) {
        return NULL;
    }
    memset(objectP, 0, sizeof(lwm2m_object_t));

    DmInstance* instance = static_cast<DmInstance*>(lwm2m_malloc(sizeof(DmInstance)));
    if (instance == NULL) {
        lwm2m_free(objectP);
        return NULL;
    }
    memset(instance, 0, sizeof(DmInstance));
    instance->id = 0;

    objectP->objID = objectId;
    objectP->versionMajor = 1;
    objectP->versionMinor = 2;
    objectP->instanceList = reinterpret_cast<lwm2m_list_t*>(instance);
    objectP->userData = state;
    return objectP;
}

DmObjectState* StateFromObject(lwm2m_object_t* objectP)
{
    return objectP == NULL ? NULL : static_cast<DmObjectState*>(objectP->userData);
}

uint8_t DeviceRead(lwm2m_context_t* contextP,
                   uint16_t instanceId,
                   int* numDataP,
                   lwm2m_data_t** dataArrayP,
                   lwm2m_object_t* objectP)
{
    (void)contextP;
    DmObjectState* state = StateFromObject(objectP);
    if (state == NULL || instanceId != 0) {
        return COAP_404_NOT_FOUND;
    }

    if (*numDataP == 0) {
        *numDataP = 4;
        *dataArrayP = lwm2m_data_new(*numDataP);
        if (*dataArrayP == NULL) {
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
        (*dataArrayP)[0].id = 0;
        (*dataArrayP)[1].id = 1;
        (*dataArrayP)[2].id = 2;
        (*dataArrayP)[3].id = 3;
    }

    for (int i = 0; i < *numDataP; ++i) {
        switch ((*dataArrayP)[i].id) {
            case 0:
                lwm2m_data_encode_string(state->config.brand.c_str(), (*dataArrayP) + i);
                break;
            case 1:
                lwm2m_data_encode_string(state->config.model.c_str(), (*dataArrayP) + i);
                break;
            case 2:
                lwm2m_data_encode_string(state->config.imei1.c_str(), (*dataArrayP) + i);
                break;
            case 3:
                lwm2m_data_encode_string(state->config.api_version.c_str(), (*dataArrayP) + i);
                break;
            default:
                return COAP_404_NOT_FOUND;
        }
    }
    return COAP_205_CONTENT;
}

uint8_t LocationRead(lwm2m_context_t* contextP,
                     uint16_t instanceId,
                     int* numDataP,
                     lwm2m_data_t** dataArrayP,
                     lwm2m_object_t* objectP)
{
    (void)contextP;
    (void)objectP;
    if (instanceId != 0) {
        return COAP_404_NOT_FOUND;
    }

    if (*numDataP == 0) {
        *numDataP = 2;
        *dataArrayP = lwm2m_data_new(*numDataP);
        if (*dataArrayP == NULL) {
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
        (*dataArrayP)[0].id = 0;
        (*dataArrayP)[1].id = 1;
    }

    for (int i = 0; i < *numDataP; ++i) {
        switch ((*dataArrayP)[i].id) {
            case 0:
            case 1:
                lwm2m_data_encode_string(kDmNoValue, (*dataArrayP) + i);
                break;
            default:
                return COAP_404_NOT_FOUND;
        }
    }
    return COAP_205_CONTENT;
}

uint8_t DmConfigRead(lwm2m_context_t* contextP,
                     uint16_t instanceId,
                     int* numDataP,
                     lwm2m_data_t** dataArrayP,
                     lwm2m_object_t* objectP)
{
    (void)contextP;
    DmObjectState* state = StateFromObject(objectP);
    if (state == NULL || instanceId != 0) {
        return COAP_404_NOT_FOUND;
    }

    if (*numDataP == 0) {
        *numDataP = 3;
        *dataArrayP = lwm2m_data_new(*numDataP);
        if (*dataArrayP == NULL) {
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
        (*dataArrayP)[0].id = kDmResourceFieldConfig;
        (*dataArrayP)[1].id = kDmResourceRuleConfig;
        (*dataArrayP)[2].id = kDmResourceAddressConfig;
    }

    for (int i = 0; i < *numDataP; ++i) {
        switch ((*dataArrayP)[i].id) {
            case kDmResourceFieldConfig:
                lwm2m_data_encode_string(state->fieldConfig.c_str(), (*dataArrayP) + i);
                break;
            case kDmResourceRuleConfig:
                lwm2m_data_encode_string(state->ruleConfig.c_str(), (*dataArrayP) + i);
                break;
            case kDmResourceAddressConfig:
                lwm2m_data_encode_string(state->addressConfig.c_str(), (*dataArrayP) + i);
                break;
            default:
                return COAP_404_NOT_FOUND;
        }
    }
    return COAP_205_CONTENT;
}

uint8_t DmConfigWrite(lwm2m_context_t* contextP,
                      uint16_t instanceId,
                      int numData,
                      lwm2m_data_t* dataArray,
                      lwm2m_object_t* objectP,
                      lwm2m_write_type_t writeType)
{
    (void)contextP;
    (void)writeType;
    DmObjectState* state = StateFromObject(objectP);
    if (state == NULL || instanceId != 0) {
        return COAP_404_NOT_FOUND;
    }

    for (int i = 0; i < numData; ++i) {
        const std::string value = DataToString(dataArray[i]);
        switch (dataArray[i].id) {
            case kDmResourceFieldConfig:
                state->fieldConfig = value;
                break;
            case kDmResourceRuleConfig:
                state->ruleConfig = value;
                break;
            case kDmResourceAddressConfig:
                state->addressConfig = value;
                if (!value.empty() && value != state->config.server_uri) {
                    state->addressChanged = true;
                }
                break;
            default:
                return COAP_404_NOT_FOUND;
        }
    }
    return COAP_204_CHANGED;
}

uint8_t DmInfoRead(lwm2m_context_t* contextP,
                   uint16_t instanceId,
                   int* numDataP,
                   lwm2m_data_t** dataArrayP,
                   lwm2m_object_t* objectP)
{
    (void)contextP;
    DmObjectState* state = StateFromObject(objectP);
    if (state == NULL || instanceId != 0) {
        return COAP_404_NOT_FOUND;
    }

    if (*numDataP == 0) {
        *numDataP = 1;
        *dataArrayP = lwm2m_data_new(*numDataP);
        if (*dataArrayP == NULL) {
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
        (*dataArrayP)[0].id = kDmResourceDeviceInfo;
    }

    for (int i = 0; i < *numDataP; ++i) {
        if ((*dataArrayP)[i].id != kDmResourceDeviceInfo) {
            return COAP_404_NOT_FOUND;
        }
        const std::string deviceInfo = BuildDmDeviceInfoJson(*state);
        AddStringResource((*dataArrayP) + i, kDmResourceDeviceInfo, deviceInfo);
    }
    return COAP_205_CONTENT;
}

}

DmRuleConfig::DmRuleConfig()
    : report_time_min(360),
      report_num(8),
      heartbeat_time_min(10),
      retry_interval_min(5),
      retry_num(3)
{
}

DmObjectState::DmObjectState()
    : addressChanged(false),
      lastReportTime(0),
      reportsInWindow(0),
      reportWindowStart(0),
      retryCount(0)
{
}

lwm2m_object_t* CreateDmDeviceObject(DmObjectState* state)
{
    lwm2m_object_t* objectP = AllocateObject(LWM2M_DEVICE_OBJECT_ID, state);
    if (objectP != NULL) {
        objectP->readFunc = DeviceRead;
    }
    return objectP;
}

lwm2m_object_t* CreateDmLocationObject(DmObjectState* state)
{
    lwm2m_object_t* objectP = AllocateObject(LWM2M_LOCATION_OBJECT_ID, state);
    if (objectP != NULL) {
        objectP->readFunc = LocationRead;
    }
    return objectP;
}

lwm2m_object_t* CreateDmConfigObject(DmObjectState* state)
{
    lwm2m_object_t* objectP = AllocateObject(kDmObjectConfig, state);
    if (objectP != NULL) {
        objectP->readFunc = DmConfigRead;
        objectP->writeFunc = DmConfigWrite;
    }
    return objectP;
}

lwm2m_object_t* CreateDmInfoObject(DmObjectState* state)
{
    lwm2m_object_t* objectP = AllocateObject(kDmObjectInfo, state);
    if (objectP != NULL) {
        objectP->readFunc = DmInfoRead;
    }
    return objectP;
}

void FreeDmObject(lwm2m_object_t* objectP)
{
    if (objectP == NULL) {
        return;
    }
    if (objectP->instanceList != NULL) {
        lwm2m_free(objectP->instanceList);
    }
    lwm2m_free(objectP);
}

DmRuleConfig ParseDmRuleConfig(const std::string& ruleConfig)
{
    DmRuleConfig cfg;
    if (ruleConfig.empty()) {
        return cfg;
    }

    cfg.report_time_min = JsonIntValue(ruleConfig, "reportTime", cfg.report_time_min);
    cfg.report_num = JsonIntValue(ruleConfig, "reportNum", cfg.report_num);
    cfg.heartbeat_time_min = JsonIntValue(ruleConfig, "heartBeatTime", cfg.heartbeat_time_min);
    cfg.retry_interval_min = JsonIntValue(ruleConfig, "retryInterval", cfg.retry_interval_min);
    cfg.retry_num = JsonIntValue(ruleConfig, "retryNum", cfg.retry_num);
    if (cfg.report_time_min <= 0) {
        cfg.report_time_min = 10;
    }
    if (cfg.report_num <= 0) {
        cfg.report_num = 8;
    }
    if (cfg.heartbeat_time_min <= 0) {
        cfg.heartbeat_time_min = 10;
    }
    if (cfg.retry_interval_min <= 0) {
        cfg.retry_interval_min = 5;
    }
    if (cfg.retry_num <= 0) {
        cfg.retry_num = 3;
    }
    return cfg;
}

std::string BuildDmDeviceInfoJson(DmObjectState& state)
{
    std::vector<std::string> fields;
    fields.push_back("imsi");
    fields.push_back("imsi2");
    fields.push_back("sn");
    fields.push_back("mac");
    fields.push_back("rom");
    fields.push_back("ram");
    fields.push_back("cpu");
    fields.push_back("sysVersion");
    fields.push_back("softwareVer");
    fields.push_back("softwareName");
    fields.push_back("volte");
    fields.push_back("netType");
    fields.push_back("phoneNumber");
    fields.push_back("batteryCapacity");
    fields.push_back("batteryCapacityCurr");
    fields.push_back("screenSize");
    fields.push_back("networkStatus");
    fields.push_back("wearingStatus");
    fields.push_back("routerMac");
    fields.push_back("bluetoothMac");
    fields.push_back("gpu");
    fields.push_back("board");
    fields.push_back("resolution");

    std::ostringstream json;
    json << "{";
    bool first = true;
    for (size_t i = 0; i < fields.size(); ++i) {
        const std::string& field = fields[i];
        if (!FieldEnabled(state.fieldConfig, field)) {
            continue;
        }

        std::string encrypted;
        const std::string plain = GetDmDeviceValue(state.config, field);
        if (DmEncryptValue(state.config.secret, plain, encrypted) != 0) {
            encrypted.clear();
        }

        if (!first) {
            json << ",";
        }
        json << "\"" << field << "\":\"" << JsonEscape(encrypted) << "\"";
        first = false;
    }
    json << "}";
    return json.str();
}

bool DmReportAllowed(DmObjectState& state, time_t now)
{
    const DmRuleConfig rule = ParseDmRuleConfig(state.ruleConfig);
    const time_t windowSec = static_cast<time_t>(rule.report_time_min) * 60;

    if (state.reportWindowStart == 0 || now - state.reportWindowStart >= windowSec) {
        state.reportWindowStart = now;
        state.reportsInWindow = 0;
    }

    if (state.reportsInWindow >= rule.report_num) {
        return false;
    }

    return true;
}

}
