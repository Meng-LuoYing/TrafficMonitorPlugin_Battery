#include "pch.h"
#include "JsonParser.h"

// ---------------------------------------------------------------
// 针对此特定 API 形状的最小化 JSON 辅助函数（无第三方依赖）
// ---------------------------------------------------------------

// 从 JSON 片段中提取 "key": "string-value"
// 返回 UTF-8 编码的 std::string，不存在或为 null 时返回空字符串
static std::string ExtractStringValue(const std::string& json, const std::string& key, size_t start = 0)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search, start);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";

    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n'))
        pos++;

    if (pos >= json.size()) return "";

    if (json.substr(pos, 4) == "null") return "";

    if (json[pos] == '"')
    {
        pos++;
        std::string val;
        while (pos < json.size() && json[pos] != '"')
        {
            if (json[pos] == '\\' && pos + 1 < json.size())
            {
                pos++; // skip backslash

                // Handle Unicode escape (both \u and \U)
                if ((json[pos] == 'u' || json[pos] == 'U') && pos + 4 < json.size())
                {
                    // Decode \uXXXX --> UTF-8 bytes
                    char hex[5] = { json[pos+1], json[pos+2], json[pos+3], json[pos+4], '\0' };
                    char* endPtr;
                    unsigned int cp = (unsigned int)strtoul(hex, &endPtr, 16);
                    
                    // Only process if we successfully parsed 4 hex digits
                    if (endPtr == hex + 4)
                    {
                        pos += 5; // skip 'u' + 4 hex digits
                        if (cp < 0x80)
                        {
                            val += (char)cp;
                        }
                        else if (cp < 0x800)
                        {
                            val += (char)(0xC0 | (cp >> 6));
                            val += (char)(0x80 | (cp & 0x3F));
                        }
                        else
                        {
                            val += (char)(0xE0 | (cp >> 12));
                            val += (char)(0x80 | ((cp >> 6) & 0x3F));
                            val += (char)(0x80 | (cp & 0x3F));
                        }
                        continue;
                    }
                }

                // Handle standard escapes
                switch (json[pos])
                {
                    case '"': val += '"'; break;
                    case '\\': val += '\\'; break;
                    case '/': val += '/'; break;
                    case 'b': val += '\b'; break;
                    case 'f': val += '\f'; break;
                    case 'n': val += '\n'; break;
                    case 'r': val += '\r'; break;
                    case 't': val += '\t'; break;
                    default:
                        // Unknown escape: keep backslash and char
                        val += '\\';
                        val += json[pos];
                        break;
                }
                pos++;
                continue;
            }
            val += json[pos++];
        }
        return val;
    }
    return "";
}

// 从 JSON 片段中提取 "key": <integer>
// 失败时返回 defVal
static int ExtractIntValue(const std::string& json, const std::string& key, size_t start, int defVal = -1)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search, start);
    if (pos == std::string::npos) return defVal;

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return defVal;

    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
        pos++;

    if (json.substr(pos, 4) == "null") return defVal;

    int val = defVal;
    try { val = std::stoi(json.substr(pos)); }
    catch (...) {}
    return val;
}

// 从 JSON 片段中提取 "key": true/false
static bool ExtractBoolValue(const std::string& json, const std::string& key, size_t start)
{
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search, start);
    if (pos == std::string::npos) return false;

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return false;

    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
        pos++;

    return json.substr(pos, 4) == "true";
}

// 使用 Windows API 将 UTF-8 std::string 转换为 std::wstring
static std::wstring Utf8ToWide(const std::string& str)
{
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &ws[0], len);
    return ws;
}

// ---------------------------------------------------------------
// 主解析器：遍历 data.devices 数组中的每个 {} 对象
// ---------------------------------------------------------------
std::vector<DeviceBattery> ParseBatteryJson(const std::string& json)
{
    std::vector<DeviceBattery> result;

    // 定位 "devices" 数组
    size_t arrStart = json.find("\"devices\"");
    if (arrStart == std::string::npos) return result;

    arrStart = json.find('[', arrStart);
    if (arrStart == std::string::npos) return result;

    size_t pos = arrStart + 1;

    while (pos < json.size())
    {
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos) break;

        // 查找匹配的右大括号（括号计数法；对此扁平 JSON 安全）
        int depth = 1;
        size_t objEnd = objStart + 1;
        while (objEnd < json.size() && depth > 0)
        {
            if (json[objEnd] == '{') depth++;
            else if (json[objEnd] == '}') depth--;
            objEnd++;
        }
        if (depth != 0) break;

        std::string obj = json.substr(objStart, objEnd - objStart);

        DeviceBattery dev;
        dev.id = Utf8ToWide(ExtractStringValue(obj, "id"));
        if (dev.id.empty())
        {
            pos = objEnd;
            size_t next = json.find_first_not_of(" \t\r\n,", pos);
            if (next != std::string::npos && json[next] == ']')
                break;
            continue;
        }

        // renamedName 优先于 name
        std::string renamedName = ExtractStringValue(obj, "renamedName");
        bool hasRenamedName = false;
        for (char ch : renamedName)
        {
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
            {
                hasRenamedName = true;
                break;
            }
        }
        if (hasRenamedName)
            dev.name = Utf8ToWide(renamedName);
        else
            dev.name = Utf8ToWide(ExtractStringValue(obj, "name"));
        if (dev.name.empty())
            dev.name = L"Battery";

        dev.battery    = ExtractIntValue(obj, "battery", 0, -1);
        dev.isCharging = ExtractBoolValue(obj, "isCharging", 0);

        std::string status = ExtractStringValue(obj, "status");
        dev.isOnline = (status == "online");

        bool unsupported = ExtractBoolValue(obj, "isBatteryUnsupported", 0);
        if (!unsupported)
            result.push_back(dev);

        pos = objEnd;

        size_t next = json.find_first_not_of(" \t\r\n,", pos);
        if (next != std::string::npos && json[next] == ']')
            break;
    }

    return result;
}
