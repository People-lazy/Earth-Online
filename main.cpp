#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <stack>
#include <regex>
#include <cstdio>

// ===================== 【核心修复】跨平台路径兼容 =====================
// 跨平台目录遍历兼容（修复Mac/Xcode dirent.h问题）
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <dirent.h>
#define CLEAR_SCREEN system("cls")
#define PATH_SEP "\\"
// 目录创建宏自动拼接程序所在目录
#define MKDIR(path) system(("mkdir " + PROGRAM_BASE_DIR + std::string(path)).c_str())
#define CHCP_UTF8 system("chcp 65001 > nul")
#else
#include <dirent.h>
#include <sys/stat.h>
#include <mach-o/dyld.h>
#define CLEAR_SCREEN system("clear")
#define PATH_SEP "/"
// 目录创建宏自动拼接程序所在目录
#define MKDIR(path) mkdir((PROGRAM_BASE_DIR + std::string(path)).c_str(), 0755)
#define CHCP_UTF8
#endif

using namespace std;

// ===================== 【核心修复】全局程序目录定义 =====================
// 全局变量：保存程序可执行文件所在的绝对目录，末尾自带路径分隔符
string PROGRAM_BASE_DIR;

// 跨平台获取可执行文件自身所在的绝对目录
string getProgramBaseDir() {
    char exePath[1024] = {0};
#if defined(_WIN32) || defined(_WIN64)
    // Windows平台：获取exe绝对路径
    GetModuleFileNameA(NULL, exePath, sizeof(exePath));
    string fullPath = exePath;
    // 去掉exe文件名，保留目录，末尾补路径分隔符
    size_t lastSepPos = fullPath.find_last_of("\\/");
    if (lastSepPos != string::npos) {
        return fullPath.substr(0, lastSepPos + 1);
    }
#else
    // Mac OS平台：获取可执行文件绝对路径
    uint32_t pathSize = sizeof(exePath);
    if (_NSGetExecutablePath(exePath, &pathSize) == 0) {
        string fullPath = exePath;
        // 去掉文件名，保留目录，末尾补路径分隔符
        size_t lastSepPos = fullPath.find_last_of('/');
        if (lastSepPos != string::npos) {
            return fullPath.substr(0, lastSepPos + 1);
        }
    }
#endif
    return "./"; // 兜底：获取失败时用当前目录
}

// 全局枚举定义
enum Era { ERA_AR, ERA_PT, ERA_PH };
enum Season { SEASON_SPRING, SEASON_SUMMER, SEASON_AUTUMN, SEASON_WINTER };
enum CivilizationLevel { CIV_PZ, CIV_MZ, CIV_CZ, CIV_MODERN, CIV_CONTEMP };
enum DisasterType { DISASTER_WEATHER, DISASTER_GEOLOGICAL };
enum TaskType { TASK_MAIN, TASK_BRANCH };
enum ModExecTime { EXEC_ON_START, EXEC_ON_SEASON, EXEC_ON_NEWYEAR, EXEC_ON_EVENT };

// 核心结构体定义
struct Trend { int startYear, endYear; string target; float value; };
struct Festival { string name, country, desc, effect; int triggerSeason, triggerYear; };
struct Disaster { string name, desc; int type, triggerSeason; float baseRate; map<string, float> effects; };
struct Task {
    int id, triggerAge;
    string name, triggerCondition, finishCondition, desc, reward;
    int type;
    float progress;
    bool isFinished;
};
struct Country {
    string name, id; int civLevel;
    float tech, economy, population, ecology, livelihood, industry, culture, disasterResist;
    vector<Festival> exclusiveFestivals; map<string, float> settings;
};
struct Player {
    string country, birthPlace; int age, currentMainTaskId;
    vector<Task> tasks; vector<string> achievements; map<string, float> attr;
};
struct World {
    int currentYear, currentSeason, currentEra, currentCentury;
    map<string, float> globalSettings; vector<Trend> trends;
    map<string, Country> countries; Player player;
    vector<string> loadedMods; bool isGameOver;
};

// MOD脚本引擎核心类
class ModScriptEngine {
public:
    map<string, float> numVars;
    map<string, string> strVars;
    World* world;
    vector<string> logBuffer;

    void init(World* w) { world = w; loadPersistentVars(); }

    string toLower(const string& s) {
        string res = s; transform(res.begin(), res.end(), res.begin(), ::tolower); return res;
    }

    string trim(const string& s) {
        size_t start = s.find_first_not_of(" \t\n\r");
        size_t end = s.find_last_not_of(" \t\n\r");
        return (start == string::npos) ? "" : s.substr(start, end - start + 1);
    }

    vector<string> split(const string& s, char delim) {
        vector<string> res; stringstream ss(s); string part;
        while (getline(ss, part, delim)) if (!trim(part).empty()) res.push_back(trim(part));
        return res;
    }

    float parseBuiltinVar(const string& varName) {
        string lowerName = toLower(varName);
        if (lowerName == "year") return world->currentYear;
        if (lowerName == "season") return world->currentSeason;
        if (lowerName == "era") return world->currentEra;
        if (lowerName == "century") return world->currentCentury;
        if (lowerName == "player_age") return world->player.age;
        if (world->globalSettings.count(lowerName)) return world->globalSettings[lowerName];
        Country& c = world->countries[world->player.country];
        if (lowerName == "country_tech") return c.tech;
        if (lowerName == "country_economy") return c.economy;
        if (lowerName == "country_ecology") return c.ecology;
        if (lowerName == "country_livelihood") return c.livelihood;
        if (lowerName == "country_population") return c.population;
        if (lowerName == "country_industry") return c.industry;
        if (lowerName == "country_culture") return c.culture;
        if (lowerName == "country_disasterresist") return c.disasterResist;
        if (numVars.count(lowerName)) return numVars[lowerName];
        return 0.0f;
    }

    bool parseCondition(const string& condition) {
        string expr = trim(condition);
        if (toLower(expr.substr(0, 3)) == "not") {
            return !parseCondition(expr.substr(3));
        }
        size_t orPos = expr.find(" OR ");
        if (orPos != string::npos) {
            return parseCondition(expr.substr(0, orPos)) || parseCondition(expr.substr(orPos + 4));
        }
        size_t andPos = expr.find(" AND ");
        if (andPos != string::npos) {
            return parseCondition(expr.substr(0, andPos)) && parseCondition(expr.substr(andPos + 5));
        }
        vector<string> ops = {">=", "<=", "==", "!=", ">", "<"};
        for (const string& op : ops) {
            size_t opPos = expr.find(op);
            if (opPos != string::npos) {
                string left = trim(expr.substr(0, opPos));
                string right = trim(expr.substr(opPos + op.size()));
                float leftVal = parseBuiltinVar(left);
                float rightVal = 0.0f;
                try { rightVal = parseBuiltinVar(right); }
                catch (...) { rightVal = stof(right); }
                if (op == ">") return leftVal > rightVal;
                if (op == "<") return leftVal < rightVal;
                if (op == ">=") return leftVal >= rightVal;
                if (op == "<=") return leftVal <= rightVal;
                if (op == "==") return abs(leftVal - rightVal) < 0.001f;
                if (op == "!=") return abs(leftVal - rightVal) >= 0.001f;
            }
        }
        return parseBuiltinVar(expr) != 0.0f;
    }

    bool execStatement(const string& stmt) {
        string line = trim(stmt);
        if (line.empty() || line[0] == '#') return true;
        vector<string> parts = split(line, ' ');
        if (parts.empty()) return true;
        string cmd = toLower(parts[0]);

        if (cmd == "var") {
            if (parts.size() < 2) return false;
            vector<string> varParts = split(parts[1], '=');
            if (varParts.size() != 2) return false;
            string varName = toLower(trim(varParts[0]));
            string varVal = trim(varParts[1]);
            try { numVars[varName] = stof(varVal); }
            catch (...) { strVars[varName] = varVal; }
            logBuffer.push_back("[VAR] 定义变量：" + varName + " = " + varVal);
            return true;
        }

        if (cmd == "set") {
            if (parts.size() < 2) return false;
            vector<string> setParts = split(parts[1], '=');
            if (setParts.size() != 2) return false;
            string key = toLower(trim(setParts[0]));
            string valStr = trim(setParts[1]);
            float val = 0.0f;
            try { val = parseBuiltinVar(valStr); }
            catch (...) { val = stof(valStr); }

            if (world->globalSettings.count(key)) {
                world->globalSettings[key] = val;
                logBuffer.push_back("[SET] 修改全局设定：" + key + " = " + to_string(val));
                return true;
            }
            Country& c = world->countries[world->player.country];
            if (key == "country_tech") { c.tech = val; return true; }
            if (key == "country_economy") { c.economy = val; return true; }
            if (key == "country_ecology") { c.ecology = val; return true; }
            if (key == "country_livelihood") { c.livelihood = val; return true; }
            if (key == "country_population") { c.population = val; return true; }
            if (key == "country_industry") { c.industry = val; return true; }
            if (key == "country_culture") { c.culture = val; return true; }
            if (key == "country_disasterresist") { c.disasterResist = val; return true; }
            numVars[key] = val;
            logBuffer.push_back("[SET] 修改变量：" + key + " = " + to_string(val));
            return true;
        }

        if (cmd == "print") {
            if (parts.size() < 2) return false;
            string content = line.substr(6);
            regex varRegex("\\$\\{([^}]+)\\}");
            smatch match;
            string::const_iterator searchStart(content.cbegin());
            while (regex_search(searchStart, content.cend(), match, varRegex)) {
                string varName = match[1].str();
                float varVal = parseBuiltinVar(varName);
                content.replace(match.position(), match.length(), to_string((int)varVal));
                searchStart = content.cbegin() + match.position() + to_string((int)varVal).size();
            }
            cout << "[MOD提示] " << content << endl;
            logBuffer.push_back("[PRINT] " + content);
            return true;
        }

        if (cmd == "input") {
            if (parts.size() < 3) return false;
            string tip = parts[1];
            string varName = toLower(parts[2]);
            float inputVal;
            cout << "[MOD交互] " << tip << "：";
            cin >> inputVal;
            numVars[varName] = inputVal;
            logBuffer.push_back("[INPUT] 玩家输入变量：" + varName + " = " + to_string(inputVal));
            return true;
        }
        return true;
    }

    void execScript(const vector<string>& lines, ModExecTime execTime) {
        stack<bool> ifStack;
        bool isInElse = false;
        bool currentBlockEnable = true;

        for (size_t i = 0; i < lines.size(); i++) {
            string line = trim(lines[i]);
            if (line.empty() || line[0] == '#') continue;
            string lowerLine = toLower(line);
            vector<string> parts = split(line, ' ');
            string cmd = toLower(parts[0]);

            if (cmd == "#exec") {
                if (parts.size() >= 2) {
                    string execTimeStr = toLower(parts[1]);
                    if ((execTimeStr == "start" && execTime != EXEC_ON_START) ||
                        (execTimeStr == "season" && execTime != EXEC_ON_SEASON) ||
                        (execTimeStr == "newyear" && execTime != EXEC_ON_NEWYEAR)) {
                        break;
                    }
                }
                continue;
            }

            if (cmd == "if") {
                size_t thenPos = lowerLine.find(" then");
                if (thenPos == string::npos) continue;
                string condition = line.substr(3, thenPos - 3);
                bool conditionResult = parseCondition(condition);
                if (!currentBlockEnable) conditionResult = false;
                ifStack.push(conditionResult);
                currentBlockEnable = conditionResult;
                isInElse = false;
                continue;
            }

            if (cmd == "else") {
                if (ifStack.empty()) continue;
                bool lastIfResult = ifStack.top();
                isInElse = true;
                currentBlockEnable = !lastIfResult;
                continue;
            }

            if (cmd == "end" && parts.size() >= 2 && toLower(parts[1]) == "if") {
                if (!ifStack.empty()) ifStack.pop();
                currentBlockEnable = ifStack.empty() ? true : ifStack.top();
                isInElse = false;
                continue;
            }

            if (!currentBlockEnable) continue;
            execStatement(line);
        }
    }

    // 【修复】MOD变量持久化路径拼接程序目录
    void savePersistentVars() {
        ofstream f(PROGRAM_BASE_DIR + "user_var" + string(PATH_SEP) + "mod_var_save.txt");
        if (!f.is_open()) return;
        f << "# MOD自定义变量持久化文件" << endl;
        f << "[数字变量]" << endl;
        for (auto& pair : numVars) f << pair.first << "=" << pair.second << endl;
        f << "[字符串变量]" << endl;
        for (auto& pair : strVars) f << pair.first << "=" << pair.second << endl;
        f.close();
    }

    // 【修复】MOD变量加载路径拼接程序目录
    void loadPersistentVars() {
        ifstream f(PROGRAM_BASE_DIR + "user_var" + string(PATH_SEP) + "mod_var_save.txt");
        if (!f.is_open()) return;
        string line, currentSection;
        while (getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            if (line[0] == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }
            vector<string> kv = split(line, '=');
            if (kv.size() != 2) continue;
            if (currentSection == "数字变量") numVars[toLower(kv[0])] = stof(kv[1]);
            if (currentSection == "字符串变量") strVars[toLower(kv[0])] = kv[1];
        }
        f.close();
    }

    // 【修复】日志写入路径拼接程序目录
    void saveLog() {
        ofstream f(PROGRAM_BASE_DIR + "log" + string(PATH_SEP) + "game_run.log", ios::app);
        if (!f.is_open()) return;
        time_t now = time(0);
        f << "===== " << ctime(&now) << " =====" << endl;
        for (auto& log : logBuffer) f << log << endl;
        f.close();
        logBuffer.clear();
    }
};

// 全局常量&数据池
const vector<string> SEASON_NAMES = {"春季", "夏季", "秋季", "冬季"};
const vector<string> ERA_NAMES = {"AR纪元(太古代/元古代)", "PT纪元(古生代/中生代)", "PH纪元(新生代)"};
const vector<string> CIV_NAMES = {"原始社会", "奴隶社会", "封建社会", "近代社会", "现代社会"};
const vector<string> COUNTRY_LIST = {
    "中国(国服)", "美国(美服)", "日本", "泰国", "俄罗斯",
    "法国", "德国", "意大利", "澳大利亚", "新加坡", "马来西亚"
};
vector<Disaster> DISASTER_LIB;
vector<Festival> FESTIVAL_LIB;
vector<Task> MAIN_TASK_LIB;
ModScriptEngine MOD_ENGINE;

// 工具函数
float randomFloat() { return (float)rand() / RAND_MAX; }
int randomInt(int min, int max) { return rand() % (max - min + 1) + min; }
void pause() { cout << "\n按回车键继续..."; cin.ignore(); cin.get(); }

// 初始化函数
// 【修复】目录创建已通过宏自动拼接程序目录，无需修改函数内容
void createDefaultDirs() {
    MKDIR("script"); MKDIR("mod"); MKDIR("saves");
    MKDIR("user_var"); MKDIR("log");
}

void initDisasterLib() {
    DISASTER_LIB.push_back({"寒潮", "春季寒潮来袭，气温骤降，农作物受损", DISASTER_WEATHER, SEASON_SPRING, 0.15f, {{"ecology", -0.1f}, {"livelihood", -0.05f}}});
    DISASTER_LIB.push_back({"沙尘暴", "强沙尘暴席卷内陆地区，空气质量骤降", DISASTER_WEATHER, SEASON_SPRING, 0.1f, {{"ecology", -0.15f}, {"livelihood", -0.1f}}});
    DISASTER_LIB.push_back({"台风", "强台风登陆沿海地区，狂风暴雨引发洪水", DISASTER_WEATHER, SEASON_SUMMER, 0.2f, {{"ecology", -0.2f}, {"economy", -0.15f}, {"livelihood", -0.2f}}});
    DISASTER_LIB.push_back({"洪涝", "持续强降雨引发洪涝灾害，农田被淹", DISASTER_WEATHER, SEASON_SUMMER, 0.18f, {{"economy", -0.2f}, {"livelihood", -0.15f}}});
    DISASTER_LIB.push_back({"暴雪", "强暴雪天气，多地积雪结冰，交通瘫痪", DISASTER_WEATHER, SEASON_WINTER, 0.15f, {{"livelihood", -0.2f}, {"economy", -0.1f}}});
    DISASTER_LIB.push_back({"地震", "突发强震，房屋倒塌，基础设施损毁", DISASTER_GEOLOGICAL, -1, 0.05f, {{"economy", -0.3f}, {"livelihood", -0.3f}, {"ecology", -0.1f}}});
    DISASTER_LIB.push_back({"火山喷发", "火山大规模喷发，火山灰覆盖周边区域", DISASTER_GEOLOGICAL, -1, 0.02f, {{"ecology", -0.4f}, {"livelihood", -0.2f}}});
    DISASTER_LIB.push_back({"海啸", "海底地震引发海啸，沿海地区被淹没", DISASTER_GEOLOGICAL, -1, 0.03f, {{"economy", -0.35f}, {"livelihood", -0.3f}}});
}

void initFestivalLib() {
    FESTIVAL_LIB.push_back({"元旦", "全球", "新年伊始，全球共同庆祝", "livelihood+0.1", SEASON_WINTER, -1});
    FESTIVAL_LIB.push_back({"劳动节", "全球", "致敬劳动者，多地放假庆祝", "livelihood+0.15", SEASON_SPRING, -1});
    FESTIVAL_LIB.push_back({"春节", "中国(国服)", "农历新年，阖家团圆", "livelihood+0.3, culture+0.2", SEASON_WINTER, -1});
    FESTIVAL_LIB.push_back({"中秋节", "中国(国服)", "中秋团圆，赏月吃月饼", "livelihood+0.25, culture+0.15", SEASON_AUTUMN, -1});
    FESTIVAL_LIB.push_back({"国庆节", "中国(国服)", "庆祝新中国成立", "livelihood+0.3, economy+0.1", SEASON_AUTUMN, 1949});
    FESTIVAL_LIB.push_back({"独立日", "美国(美服)", "庆祝美国独立", "livelihood+0.25, culture+0.15", SEASON_SUMMER, 1776});
}

// ✅ 修复：Task结构体初始化顺序完全匹配声明
void initMainTaskLib() {
    MAIN_TASK_LIB.push_back({
        1, 0,
        "学会基本技能",
        "年龄0-3岁",
        "完成行走、语言、自理学习",
        "婴儿阶段基础生存技能学习",
        "解锁基础交互权限，获得「人生第一步」成就",
        TASK_MAIN,
        0.0f,
        false
    });
    MAIN_TASK_LIB.push_back({
        2, 6,
        "系统学习知识",
        "年龄6-22岁",
        "完成全学段学业",
        "积累知识储备，搭建认知体系",
        "解锁职场择业权限，获得「学有所成」成就",
        TASK_MAIN,
        0.0f,
        false
    });
    MAIN_TASK_LIB.push_back({
        3, 22,
        "步入职场工作",
        "完成学业",
        "累计5年职场经验",
        "实现经济独立，积累职场经验",
        "解锁家庭组建权限，获得「职场精英」成就",
        TASK_MAIN,
        0.0f,
        false
    });
    MAIN_TASK_LIB.push_back({
        4, 22,
        "组建家庭结婚",
        "完成职场入门",
        "组建新生家庭",
        "完成人生成家阶段目标",
        "解锁子女养育权限，获得「阖家美满」成就",
        TASK_MAIN,
        0.0f,
        false
    });
    MAIN_TASK_LIB.push_back({
        5, 25,
        "孕育养育子女",
        "完成结婚",
        "养育子女至18岁",
        "完成人生代际传承",
        "解锁人生圆满结局，获得「代代相传」成就",
        TASK_MAIN,
        0.0f,
        false
    });
}

void initCountries(World& world) {
    for (const string& name : COUNTRY_LIST) {
        Country c;
        c.name = name;
        c.id = name;
        c.civLevel = CIV_CONTEMP;
        c.tech = 1.0f;
        c.economy = 1.0f;
        c.population = 1.0f;
        c.ecology = 1.0f;
        c.livelihood = 1.0f;
        c.industry = 1.0f;
        c.culture = 1.0f;
        c.disasterResist = 1.0f;
        world.countries[name] = c;
    }
}

// 【修复】默认剧本创建路径拼接程序目录
void createDefaultScript() {
    string path = PROGRAM_BASE_DIR + "script" + string(PATH_SEP) + "main_story.txt";
    ifstream f(path); if (f.good()) { f.close(); return; }
    ofstream out(path);
    out << "# 地球Online 主世界核心剧本" << endl;
    out << "START_YEAR=2026" << endl;
    out << "START_SEASON=春季" << endl;
    out << "START_ERA=PH纪元" << endl;
    out << "START_CENTURY=21" << endl;
    out << "DEFAULT_COUNTRY=中国(国服)" << endl;
    out << "BASE_TECH_SPEED=1.0" << endl;
    out << "BASE_ECONOMY_SPEED=1.0" << endl;
    out << "BASE_DISASTER_RATE=0.7" << endl;
    out << "BASE_ECOLOGY=1.0" << endl;
    out << "BASE_LIVELIHOOD=1.0" << endl;
    out << "BASE_POPULATION_GROWTH=1.0" << endl;
    out << "BASE_INDUSTRY_SPEED=1.0" << endl;
    out << "BASE_CULTURE_SPEED=1.0" << endl;
    out << "ERA_AR_END_YEAR=3800000000" << endl;
    out << "ERA_PT_END_YEAR=66000000" << endl;
    out << "SPRING_ECO_BONUS=0.1" << endl;
    out << "SUMMER_CULTURE_BONUS=0.1" << endl;
    out << "AUTUMN_ECONOMY_BONUS=0.15" << endl;
    out << "WINTER_LIVELIHOOD_BONUS=0.05" << endl;
    out.close();
}

// 【修复】主剧本加载路径拼接程序目录
bool loadMainScript(World& world) {
    string path = PROGRAM_BASE_DIR + "script" + string(PATH_SEP) + "main_story.txt";
    ifstream f(path); if (!f.is_open()) return false;
    string line;
    while (getline(f, line)) {
        line = MOD_ENGINE.trim(line);
        if (line.empty() || line[0] == '#') continue;
        vector<string> kv = MOD_ENGINE.split(line, '=');
        if (kv.size() != 2) continue;
        string key = MOD_ENGINE.toLower(kv[0]);
        string val = kv[1];
        if (key == "start_year") world.currentYear = stoi(val);
        else if (key == "start_season") {
            for (int i=0; i<4; i++) if (SEASON_NAMES[i] == val) world.currentSeason = i;
        }
        else if (key == "start_era") {
            for (int i=0; i<3; i++) if (ERA_NAMES[i] == val) world.currentEra = i;
        }
        else if (key == "start_century") world.currentCentury = stoi(val);
        else if (key == "default_country") world.player.country = val;
        else world.globalSettings[key] = stof(val);
    }
    f.close(); return true;
}

vector<string> loadModFile(const string& path) {
    vector<string> lines;
    ifstream f(path); if (!f.is_open()) return lines;
    string line;
    while (getline(f, line)) lines.push_back(line);
    f.close(); return lines;
}

// 【修复】MOD加载路径拼接程序目录
void loadAllMods(World& world, ModExecTime execTime) {
    if (execTime == EXEC_ON_START) {
        cout << "正在加载MOD模组..." << endl;
        world.loadedMods.clear();
    }
    string modDir = PROGRAM_BASE_DIR + "mod";
    DIR* dir = opendir(modDir.c_str());
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        string name = entry->d_name;
        if (name == "." || name == "..") continue;
        string fullPath = modDir + PATH_SEP + name;
        if (entry->d_type == DT_DIR) continue;
        if (name.size() > 4 && name.substr(name.size()-4) == ".txt") {
            vector<string> lines = loadModFile(fullPath);
            if (!lines.empty()) {
                MOD_ENGINE.execScript(lines, execTime);
                if (execTime == EXEC_ON_START) {
                    world.loadedMods.push_back(name);
                    cout << "[已加载] " << name << endl;
                }
            }
        }
    }
    closedir(dir);
    if (execTime == EXEC_ON_START) MOD_ENGINE.saveLog();
}

// 【修复】存档加载路径拼接程序目录
bool loadSave(const string& saveName, World& world) {
    string path = PROGRAM_BASE_DIR + "saves" + string(PATH_SEP) + saveName + ".txt";
    ifstream f(path); if (!f.is_open()) return false;
    World newWorld; initCountries(newWorld);
    string line, currentSection;
    while (getline(f, line)) {
        line = MOD_ENGINE.trim(line);
        if (line.empty()) continue;
        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size()-2);
            continue;
        }
        vector<string> kv = MOD_ENGINE.split(line, '=');
        if (kv.size() != 2) continue;
        string key = kv[0], val = kv[1];
        if (currentSection == "世界基础信息") {
            if (key == "CURRENT_YEAR") newWorld.currentYear = stoi(val);
            else if (key == "CURRENT_SEASON") {
                for (int i=0; i<4; i++) if (SEASON_NAMES[i] == val) newWorld.currentSeason = i;
            }
            else if (key == "CURRENT_ERA") {
                for (int i=0; i<3; i++) if (ERA_NAMES[i] == val) newWorld.currentEra = i;
            }
            else if (key == "CURRENT_CENTURY") newWorld.currentCentury = stoi(val);
        }
        else if (currentSection == "玩家信息") {
            if (key == "PLAYER_COUNTRY") newWorld.player.country = val;
            else if (key == "PLAYER_BIRTHPLACE") newWorld.player.birthPlace = val;
            else if (key == "PLAYER_AGE") newWorld.player.age = stoi(val);
        }
        else if (currentSection == "全局设定") {
            newWorld.globalSettings[MOD_ENGINE.toLower(key)] = stof(val);
        }
        else if (currentSection == "国家数据") {
            vector<string> keyParts = MOD_ENGINE.split(key, '_');
            if (keyParts.size() >=3) {
                string countryName = keyParts[1];
                string attr = MOD_ENGINE.toLower(keyParts[2]);
                if (newWorld.countries.count(countryName)) {
                    Country& c = newWorld.countries[countryName];
                    if (attr == "tech") c.tech = stof(val);
                    else if (attr == "economy") c.economy = stof(val);
                    else if (attr == "ecology") c.ecology = stof(val);
                    else if (attr == "livelihood") c.livelihood = stof(val);
                    else if (attr == "civlevel") c.civLevel = stoi(val);
                }
            }
        }
        else if (currentSection == "MOD自定义变量") {
            MOD_ENGINE.numVars[MOD_ENGINE.toLower(key)] = stof(val);
        }
    }
    f.close();
    newWorld.isGameOver = false;
    world = newWorld;
    MOD_ENGINE.world = &world;
    return true;
}

// 【修复】存档保存路径拼接程序目录
bool saveGame(const string& saveName, World& world) {
    string path = PROGRAM_BASE_DIR + "saves" + string(PATH_SEP) + saveName + ".txt";
    ofstream f(path, ios::trunc);
    if (!f.is_open()) return false;
    f << "[世界基础信息]" << endl;
    f << "CURRENT_YEAR=" << world.currentYear << endl;
    f << "CURRENT_SEASON=" << SEASON_NAMES[world.currentSeason] << endl;
    f << "CURRENT_ERA=" << ERA_NAMES[world.currentEra] << endl;
    f << "CURRENT_CENTURY=" << world.currentCentury << endl;
    f << endl << "[玩家信息]" << endl;
    f << "PLAYER_COUNTRY=" << world.player.country << endl;
    f << "PLAYER_BIRTHPLACE=" << world.player.birthPlace << endl;
    f << "PLAYER_AGE=" << world.player.age << endl;
    f << endl << "[全局设定]" << endl;
    for (auto& pair : world.globalSettings) f << pair.first << "=" << pair.second << endl;
    f << endl << "[国家数据]" << endl;
    for (auto& pair : world.countries) {
        Country& c = pair.second;
        f << "COUNTRY_" << c.name << "_TECH=" << c.tech << endl;
        f << "COUNTRY_" << c.name << "_ECONOMY=" << c.economy << endl;
        f << "COUNTRY_" << c.name << "_ECOLOGY=" << c.ecology << endl;
        f << "COUNTRY_" << c.name << "_LIVELIHOOD=" << c.livelihood << endl;
        f << "COUNTRY_" << c.name << "_CIVLEVEL=" << c.civLevel << endl;
    }
    f << endl << "[MOD自定义变量]" << endl;
    for (auto& pair : MOD_ENGINE.numVars) f << pair.first << "=" << pair.second << endl;
    f.close();
    MOD_ENGINE.savePersistentVars();
    return true;
}

// 核心游戏逻辑
void applyTrends(World& world) {
    for (auto& trend : world.trends) {
        if (world.currentYear >= trend.startYear && world.currentYear <= trend.endYear) {
            if (trend.target == "科技研发速度") world.globalSettings["base_tech_speed"] *= trend.value;
            if (trend.target == "经济发展水平") world.globalSettings["base_economy_speed"] *= trend.value;
        }
    }
}

void applySeasonEffect(World& world) {
    Country& c = world.countries[world.player.country];
    cout << "\n===== " << SEASON_NAMES[world.currentSeason] << "特性生效 =====" << endl;
    switch (world.currentSeason) {
        case SEASON_SPRING:
            c.ecology += world.globalSettings["spring_eco_bonus"];
            c.industry += 0.05f;
            cout << "万物复苏，生态环境提升，工业开工率上升" << endl;
            break;
        case SEASON_SUMMER:
            c.industry -= 0.05f;
            c.culture += world.globalSettings["summer_culture_bonus"];
            cout << "夏季高温，户外劳作效率下降，文化活动活跃度上升" << endl;
            break;
        case SEASON_AUTUMN:
            c.economy += world.globalSettings["autumn_economy_bonus"];
            c.livelihood += 0.1f;
            cout << "秋收丰收，经济收入提升，民生幸福度上涨" << endl;
            break;
        case SEASON_WINTER:
            c.industry -= 0.05f;
            c.livelihood += world.globalSettings["winter_livelihood_bonus"];
            cout << "冬季严寒，户外生产减少，家庭团聚活动增多" << endl;
            break;
    }
}

void triggerFestivals(World& world) {
    Country& c = world.countries[world.player.country];
    for (auto& festival : FESTIVAL_LIB) {
        if (festival.country != "全球" && festival.country != c.name) continue;
        if (festival.triggerSeason != -1 && festival.triggerSeason != world.currentSeason) continue;
        if (festival.triggerYear != -1 && world.currentYear != festival.triggerYear) continue;
        cout << "\n===== 节日触发：" << festival.name << " =====" << endl;
        cout << festival.desc << endl;
        vector<string> effects = MOD_ENGINE.split(festival.effect, ',');
        for (auto& eff : effects) {
            vector<string> effParts = MOD_ENGINE.split(eff, '+');
            if (effParts.size() == 2) {
                string attr = MOD_ENGINE.toLower(effParts[0]);
                float val = stof(effParts[1]);
                if (attr == "livelihood") c.livelihood += val;
                if (attr == "economy") c.economy += val;
                if (attr == "culture") c.culture += val;
            }
        }
    }
}

void triggerDisasters(World& world) {
    Country& c = world.countries[world.player.country];
    float globalRate = world.globalSettings["base_disaster_rate"];
    for (auto& disaster : DISASTER_LIB) {
        if (disaster.triggerSeason != -1 && disaster.triggerSeason != world.currentSeason) continue;
        float finalRate = disaster.baseRate * globalRate * (1.0f / c.disasterResist);
        if (randomFloat() <= finalRate) {
            cout << "\n===== 灾害预警：" << disaster.name << " =====" << endl;
            cout << disaster.desc << endl;
            for (auto& effect : disaster.effects) {
                string attr = effect.first;
                float loss = effect.second;
                if (c.tech > 1.0f) loss *= (1.0f / c.tech);
                if (attr == "ecology") c.ecology += loss;
                if (attr == "economy") c.economy += loss;
                if (attr == "livelihood") c.livelihood += loss;
            }
        }
    }
}

void checkEraIteration(World& world) {
    if (world.currentEra == ERA_AR && world.currentYear >= world.globalSettings["era_ar_end_year"]) {
        world.currentEra = ERA_PT;
        cout << "\n===== 纪元迭代：进入PT纪元 =====" << endl;
    }
    if (world.currentEra == ERA_PT && world.currentYear >= world.globalSettings["era_pt_end_year"]) {
        world.currentEra = ERA_PH;
        cout << "\n===== 纪元迭代：进入PH纪元 =====" << endl;
        for (auto& pair : world.countries) pair.second.civLevel = CIV_PZ;
    }
}

void updateTaskSystem(World& world) {
    if (world.player.country != "中国(国服)") return;
    Player& p = world.player;
    if (p.tasks.empty()) {
        for (auto& task : MAIN_TASK_LIB) p.tasks.push_back(task);
        p.currentMainTaskId = 1;
    }
    for (auto& task : p.tasks) {
        if (task.isFinished) continue;
        if (p.age < task.triggerAge) continue;
        if (task.type == TASK_MAIN) {
            if (task.id == 1 && p.age >= 3) task.progress = 100.0f;
            if (task.id == 2 && p.age >= 22) task.progress = 100.0f;
            if (task.id == 3 && p.age >= 27) task.progress = 100.0f;
            if (task.id == 4 && p.age >= 30) task.progress = 100.0f;
            if (task.id == 5 && p.age >= 43) task.progress = 100.0f;
            if (task.progress >= 100.0f && !task.isFinished) {
                task.isFinished = true;
                cout << "\n===== 主线任务完成：" << task.name << " =====" << endl;
                cout << "奖励：" << task.reward << endl;
                p.achievements.push_back(task.reward);
                p.currentMainTaskId = task.id + 1;
            }
        }
    }
    if (p.age >= 100) {
        world.isGameOver = true;
        cout << "\n===== 游戏结束 =====" << endl;
        cout << "你的100年人生已走到终点，感谢游玩！" << endl;
        pause(); exit(0);
    }
}

void advanceSeason(World& world) {
    world.currentSeason = (world.currentSeason + 1) % 4;
    bool isNewYear = (world.currentSeason == 0);
    if (isNewYear) {
        world.currentYear++;
        world.player.age++;
        world.currentCentury = (world.currentYear / 100) + 1;
        cout << "\n===== 新年伊始：" << world.currentYear << "年 =====" << endl;
        applyTrends(world);
        checkEraIteration(world);
        loadAllMods(world, EXEC_ON_NEWYEAR);
    }
    applySeasonEffect(world);
    triggerFestivals(world);
    triggerDisasters(world);
    updateTaskSystem(world);
    loadAllMods(world, EXEC_ON_SEASON);
    MOD_ENGINE.saveLog();
}

// 菜单交互
void showWorldStatus(World& world) {
    CLEAR_SCREEN;
    Country& c = world.countries[world.player.country];
    cout << "===== 地球Online 世界状态 =====" << endl;
    cout << "当前时间：" << world.currentYear << "年 " << SEASON_NAMES[world.currentSeason] << endl;
    cout << "当前纪元：" << ERA_NAMES[world.currentEra] << " | 文明阶段：" << CIV_NAMES[c.civLevel] << endl;
    cout << "所属国家：" << c.name << " | 出生地点：" << world.player.birthPlace << endl;
    cout << "玩家年龄：" << world.player.age << "岁 | 已加载MOD：" << world.loadedMods.size() << "个" << endl;
    cout << "\n===== 国家核心属性 =====" << endl;
    cout << "科技水平：" << c.tech << "x | 经济水平：" << c.economy << "x" << endl;
    cout << "生态环境：" << c.ecology << "x | 民生幸福：" << c.livelihood << "x" << endl;
    cout << "工业水平：" << c.industry << "x | 文化水平：" << c.culture << "x" << endl;
}

void mainMenu(World& world);

void settingMenu(World& world) {
    CLEAR_SCREEN;
    cout << "===== 设定修改面板 =====" << endl;
    if (world.currentSeason != SEASON_WINTER) {
        cout << "提示：仅冬季年末结算节点开放设定修改权限！" << endl;
        pause(); return;
    }
    cout << "1. 修改全局设定" << endl;
    cout << "2. 修改国家专属设定" << endl;
    cout << "3. 添加长期趋势设定" << endl;
    cout << "4. 返回主菜单" << endl;
    cout << "请输入选择：";
    int choice; cin >> choice;
    if (choice == 1) {
        string key; float val;
        cout << "输入设定项（如base_tech_speed）："; cin >> key;
        cout << "输入新数值："; cin >> val;
        world.globalSettings[MOD_ENGINE.toLower(key)] = val;
        cout << "修改成功！" << endl;
    }
    if (choice == 2) {
        Country& c = world.countries[world.player.country];
        string attr; float val;
        cout << "输入属性（如tech/economy）："; cin >> attr;
        cout << "输入新数值："; cin >> val;
        attr = MOD_ENGINE.toLower(attr);
        if (attr == "tech") c.tech = val;
        if (attr == "economy") c.economy = val;
        cout << "修改成功！" << endl;
    }
    pause();
    mainMenu(world);
}

void mainMenu(World& world) {
    while (!world.isGameOver) {
        CLEAR_SCREEN;
        cout << "===== 地球Online 主菜单 =====" << endl;
        cout << "当前时间：" << world.currentYear << "年 " << SEASON_NAMES[world.currentSeason] << endl;
        cout << "所属国家：" << world.player.country << " | 玩家年龄：" << world.player.age << "岁" << endl;
        cout << "--------------------------------" << endl;
        cout << "1. 推进到下一个季节" << endl;
        cout << "2. 查看世界状态" << endl;
        cout << "3. 查看任务面板" << endl;
        cout << "4. 设定修改（年末开放）" << endl;
        cout << "5. 保存游戏" << endl;
        cout << "6. 加载存档" << endl;
        cout << "7. 退出游戏" << endl;
        cout << "--------------------------------" << endl;
        cout << "请输入选择：";
        int choice; cin >> choice;
        switch (choice) {
            case 1: advanceSeason(world); pause(); break;
            case 2: showWorldStatus(world); pause(); break;
            case 4: settingMenu(world); break;
            case 5: {
                string saveName; cout << "输入存档名称："; cin >> saveName;
                saveGame(saveName, world) ? cout << "存档成功！" << endl : cout << "存档失败！" << endl;
                pause(); break;
            }
            case 6: {
                string saveName; cout << "输入存档名称："; cin >> saveName;
                loadSave(saveName, world) ? cout << "加载成功！" << endl : cout << "加载失败！" << endl;
                pause(); break;
            }
            case 7: exit(0);
            default: cout << "输入错误！" << endl; pause(); break;
        }
    }
}

void newGame(World& world) {
    CLEAR_SCREEN;
    cout << "===== 新建游戏 =====" << endl;
    cout << "\n选择所属国家（输入序号）：" << endl;
    for (int i=0; i<COUNTRY_LIST.size(); i++) cout << i+1 << ". " << COUNTRY_LIST[i] << endl;
    int idx; while (true) { cin >> idx; if (idx>=1 && idx<=COUNTRY_LIST.size()) break; cout << "输入错误："; }
    world.player.country = COUNTRY_LIST[idx-1];
    if (world.player.country == "中国(国服)") {
        cout << "\n输入出生地点（省份+地市）：";
        cin.ignore(); getline(cin, world.player.birthPlace);
    } else world.player.birthPlace = "无";
    cout << "\n选择起始纪元（输入序号）：" << endl;
    for (int i=0; i<ERA_NAMES.size(); i++) cout << i+1 << ". " << ERA_NAMES[i] << endl;
    while (true) { cin >> idx; if (idx>=1 && idx<=ERA_NAMES.size()) break; cout << "输入错误："; }
    world.currentEra = idx-1;
    cout << "\n输入起始年份："; cin >> world.currentYear;
    world.currentCentury = (world.currentYear / 100) + 1;
    world.player.age = 0;
    world.isGameOver = false;
    cout << "\n游戏创建完成！即将进入主菜单..." << endl;
    pause();
    mainMenu(world);
}

void startMenu() {
    while (true) {
        CLEAR_SCREEN;
        cout << "========================================" << endl;
        cout << "          地球Online：现实演化版        " << endl;
        cout << "========================================" << endl;
        cout << "                1. 新建游戏             " << endl;
        cout << "                2. 加载存档             " << endl;
        cout << "                3. 退出游戏             " << endl;
        cout << "========================================" << endl;
        cout << "请输入选择：";
        int choice; cin >> choice;
        World world; initCountries(world);
        loadMainScript(world); MOD_ENGINE.init(&world);
        loadAllMods(world, EXEC_ON_START);
        switch (choice) {
            case 1: newGame(world); break;
            case 2: {
                string saveName; cout << "输入存档名称："; cin >> saveName;
                if (loadSave(saveName, world)) { pause(); mainMenu(world); }
                else { cout << "加载失败！" << endl; pause(); }
                break;
            }
            case 3: exit(0);
            default: cout << "输入错误！" << endl; pause(); break;
        }
    }
}

// 主函数
int main() {
    CHCP_UTF8;
    // 【必须放在最前面】启动时先锁定程序所在的绝对目录，解决路径错乱问题
    PROGRAM_BASE_DIR = getProgramBaseDir();
    srand((unsigned int)time(NULL));
    
    // 后续所有逻辑不变
    createDefaultDirs();
    createDefaultScript();
    initDisasterLib();
    initFestivalLib();
    initMainTaskLib();
    startMenu();
    return 0;
}
