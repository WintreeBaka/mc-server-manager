#include "config/ConfigSchema.h"

namespace mcsm {

namespace {

ConfigField makeText(const QString &key, const QString &group, const QString &label,
                     const QString &defaultValue, const QString &hint = QString(),
                     bool advanced = false)
{
    ConfigField field;
    field.key = key;
    field.group = group;
    field.label = label;
    field.defaultValue = defaultValue;
    field.hint = hint;
    field.type = ConfigFieldType::Text;
    field.advanced = advanced;
    return field;
}

ConfigField makeInt(const QString &key, const QString &group, const QString &label,
                    const QString &defaultValue, int min, int max, const QString &unit = QString(),
                    bool advanced = false, const QString &hint = QString())
{
    ConfigField field;
    field.key = key;
    field.group = group;
    field.label = label;
    field.defaultValue = defaultValue;
    field.min = min;
    field.max = max;
    field.unit = unit;
    field.hint = hint;
    field.type = ConfigFieldType::Integer;
    field.advanced = advanced;
    return field;
}

ConfigField makeBool(const QString &key, const QString &group, const QString &label, bool defaultValue,
                     const QString &hint = QString(), bool advanced = false)
{
    ConfigField field;
    field.key = key;
    field.group = group;
    field.label = label;
    field.defaultValue = defaultValue ? QStringLiteral("true") : QStringLiteral("false");
    field.hint = hint;
    field.type = ConfigFieldType::Boolean;
    field.advanced = advanced;
    return field;
}

ConfigField makeChoice(const QString &key, const QString &group, const QString &label,
                       const QString &defaultValue, const QStringList &options,
                       const QString &hint = QString())
{
    ConfigField field;
    field.key = key;
    field.group = group;
    field.label = label;
    field.defaultValue = defaultValue;
    field.options = options;
    field.hint = hint;
    field.type = ConfigFieldType::Choice;
    return field;
}

QVector<ConfigField> buildFields()
{
    QVector<ConfigField> fields;
    const QString basic = QStringLiteral("基础");
    const QString world = QStringLiteral("世界");
    const QString perf = QStringLiteral("性能");
    const QString advanced = QStringLiteral("进阶");

    fields << makeText(QStringLiteral("motd"), basic, QStringLiteral("服务器标语 (MOTD)"),
                       QStringLiteral("A Minecraft Server"),
                       QStringLiteral("支持 & 颜色代码，显示在服务器列表中"))
           << makeInt(QStringLiteral("server-port"), basic, QStringLiteral("监听端口"),
                      QStringLiteral("25565"), 1024, 65535, QStringLiteral("端口"), true,
                      QStringLiteral("修改后需要同步调整容器端口映射"))
           << makeInt(QStringLiteral("max-players"), basic, QStringLiteral("最大玩家数"),
                      QStringLiteral("20"), 1, 1000)
           << makeChoice(QStringLiteral("gamemode"), basic, QStringLiteral("默认游戏模式"),
                         QStringLiteral("survival"),
                         {QStringLiteral("survival"), QStringLiteral("creative"),
                          QStringLiteral("adventure"), QStringLiteral("spectator")})
           << makeChoice(QStringLiteral("difficulty"), basic, QStringLiteral("默认难度"),
                         QStringLiteral("easy"),
                         {QStringLiteral("peaceful"), QStringLiteral("easy"),
                          QStringLiteral("normal"), QStringLiteral("hard")})
           << makeText(QStringLiteral("level-name"), basic, QStringLiteral("世界名称"),
                       QStringLiteral("world"), QStringLiteral("同时也是存档目录名"))
           << makeText(QStringLiteral("level-seed"), basic, QStringLiteral("世界种子"), QString(),
                       QStringLiteral("留空表示随机生成"))
           << makeBool(QStringLiteral("online-mode"), basic, QStringLiteral("正版验证"), true,
                       QStringLiteral("关闭后非正版客户端也能进入，请谨慎"))
           << makeBool(QStringLiteral("pvp"), basic, QStringLiteral("允许 PVP"), true)
           << makeBool(QStringLiteral("white-list"), basic, QStringLiteral("启用白名单"), false)
           << makeBool(QStringLiteral("enforce-whitelist"), basic, QStringLiteral("强制白名单"), false)
           << makeBool(QStringLiteral("hardcore"), basic, QStringLiteral("极限模式"), false)
           << makeChoice(QStringLiteral("level-type"), world, QStringLiteral("世界类型"),
                         QStringLiteral("minecraft:normal"),
                         {QStringLiteral("minecraft:normal"), QStringLiteral("minecraft:flat"),
                          QStringLiteral("minecraft:large_biomes"), QStringLiteral("minecraft:amplified"),
                          QStringLiteral("minecraft:single_biome_surface")})
           << makeBool(QStringLiteral("generate-structures"), world, QStringLiteral("生成建筑结构"), true)
           << makeBool(QStringLiteral("allow-nether"), world, QStringLiteral("允许下界"), true)
           << makeInt(QStringLiteral("spawn-protection"), world, QStringLiteral("出生点保护半径"),
                      QStringLiteral("16"), 0, 1000, QStringLiteral("格"))
           << makeBool(QStringLiteral("allow-flight"), world, QStringLiteral("允许飞行"), false)
           << makeInt(QStringLiteral("max-world-size"), world, QStringLiteral("世界边界半径"),
                      QStringLiteral("29999984"), 1, 29999984, QStringLiteral("格"), true)
           << makeInt(QStringLiteral("view-distance"), perf, QStringLiteral("视距"),
                      QStringLiteral("10"), 3, 32, QStringLiteral("区块"), false,
                      QStringLiteral("数值越大越吃 CPU 与带宽"))
           << makeInt(QStringLiteral("simulation-distance"), perf, QStringLiteral("模拟距离"),
                      QStringLiteral("10"), 3, 32, QStringLiteral("区块"), false,
                      QStringLiteral("影响生物与红石的活动范围"))
           << makeInt(QStringLiteral("max-tick-time"), perf, QStringLiteral("单刻超时 (毫秒)"),
                      QStringLiteral("60000"), -1, 600000, QStringLiteral("ms"), true,
                      QStringLiteral("设为 -1 可关闭看门狗，避免大型整合包被误杀"))
           << makeInt(QStringLiteral("entity-broadcast-range-percentage"), perf,
                      QStringLiteral("实体广播范围"), QStringLiteral("100"), 10, 1000,
                      QStringLiteral("%"), true)
           << makeInt(QStringLiteral("network-compression-threshold"), perf,
                      QStringLiteral("网络压缩阈值"), QStringLiteral("256"), -1, 2097152,
                      QStringLiteral("字节"), true)
           << makeBool(QStringLiteral("sync-chunk-writes"), perf, QStringLiteral("同步区块写入"),
                       true, QStringLiteral("机械硬盘建议开启，固态可关闭以提升性能"), true)
           << makeInt(QStringLiteral("player-idle-timeout"), perf, QStringLiteral("挂机踢出时间"),
                      QStringLiteral("0"), 0, 1440, QStringLiteral("分钟"), true,
                      QStringLiteral("0 表示不踢出"))
           << makeBool(QStringLiteral("enable-command-block"), advanced,
                       QStringLiteral("允许命令方块"), false)
           << makeInt(QStringLiteral("op-permission-level"), advanced, QStringLiteral("OP 权限等级"),
                      QStringLiteral("4"), 1, 4)
           << makeInt(QStringLiteral("function-permission-level"), advanced,
                      QStringLiteral("函数权限等级"), QStringLiteral("2"), 1, 4)
           << makeBool(QStringLiteral("enable-rcon"), advanced, QStringLiteral("启用 RCON"), true,
                       QStringLiteral("管理器通过 RCON 发送控制台指令，建议保持开启"), true)
           << makeInt(QStringLiteral("rcon.port"), advanced, QStringLiteral("RCON 端口"),
                      QStringLiteral("25575"), 1024, 65535, QStringLiteral("端口"), true)
           << makeBool(QStringLiteral("enable-query"), advanced, QStringLiteral("启用 Query"),
                       false, QString(), true)
           << makeInt(QStringLiteral("query.port"), advanced, QStringLiteral("Query 端口"),
                      QStringLiteral("25565"), 1024, 65535, QStringLiteral("端口"), true)
           << makeBool(QStringLiteral("enforce-secure-profile"), advanced,
                       QStringLiteral("强制安全档案"), true,
                       QStringLiteral("关闭后离线客户端也能发言"), true)
           << makeBool(QStringLiteral("log-ips"), advanced, QStringLiteral("记录玩家 IP"), true,
                       QString(), true)
           << makeInt(QStringLiteral("pause-when-empty-seconds"), advanced,
                      QStringLiteral("空服自动暂停"), QStringLiteral("0"), 0, 86400,
                      QStringLiteral("秒"), true, QStringLiteral("0 表示不暂停 (1.21+)"));
    return fields;
}

} // namespace

QJsonObject ConfigField::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("key"), key);
    object.insert(QStringLiteral("label"), label);
    object.insert(QStringLiteral("group"), group);
    object.insert(QStringLiteral("hint"), hint);
    object.insert(QStringLiteral("default"), defaultValue);
    object.insert(QStringLiteral("unit"), unit);
    object.insert(QStringLiteral("min"), min);
    object.insert(QStringLiteral("max"), max);
    object.insert(QStringLiteral("advanced"), advanced);
    object.insert(QStringLiteral("restartRequired"), restartRequired);
    QString type;
    switch (this->type) {
    case ConfigFieldType::Text: type = QStringLiteral("text"); break;
    case ConfigFieldType::Integer: type = QStringLiteral("int"); break;
    case ConfigFieldType::Boolean: type = QStringLiteral("bool"); break;
    case ConfigFieldType::Choice: type = QStringLiteral("choice"); break;
    case ConfigFieldType::Password: type = QStringLiteral("password"); break;
    case ConfigFieldType::Decimal: type = QStringLiteral("decimal"); break;
    }
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("options"), QJsonArray::fromStringList(options));
    return object;
}

const QVector<ConfigField> &ConfigSchema::fields()
{
    static const QVector<ConfigField> data = buildFields();
    return data;
}

QJsonArray ConfigSchema::toJson()
{
    QJsonArray array;
    for (const ConfigField &field : fields())
        array.append(field.toJson());
    return array;
}

QStringList ConfigSchema::groups()
{
    QStringList groups;
    for (const ConfigField &field : fields()) {
        if (!groups.contains(field.group))
            groups << field.group;
    }
    return groups;
}

const ConfigField *ConfigSchema::find(const QString &key)
{
    for (const ConfigField &field : fields()) {
        if (field.key == key)
            return &field;
    }
    return nullptr;
}

QString ConfigSchema::canonicalKey(const QString &key)
{
    const QString trimmed = key.trimmed();
    for (const ConfigField &field : fields()) {
        if (field.key.compare(trimmed, Qt::CaseInsensitive) == 0)
            return field.key;
    }
    return trimmed;
}

} // namespace mcsm
