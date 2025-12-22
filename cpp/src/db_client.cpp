#include "db_client.h"
#include <sqlite3.h>
#include <regex>
#include <algorithm>
#include <iostream>

#ifdef HAVE_POSTGRESQL
#include <libpq-fe.h>
#endif

#ifdef HAVE_MYSQL
#include <mysql/mysql.h>
#endif

namespace oleg {

// ============================================================================
// DBConnection Implementation
// ============================================================================

DBConnection DBConnection::parse(const std::string& type, const std::string& conn_string) {
    DBConnection conn;
    conn.type = type;
    conn.port = 0;

    if (type == "sqlite") {
        conn.path = conn_string;
        return conn;
    }

    // Parse connection string: host:port/database?user=xxx&password=xxx
    // Or: postgresql://user:password@host:port/database
    // Or: mysql://user:password@host:port/database

    std::regex url_regex(R"((\w+)://(?:([^:]+):([^@]+)@)?([^:/]+)(?::(\d+))?(?:/(.+))?)");
    std::smatch match;

    if (std::regex_match(conn_string, match, url_regex)) {
        conn.username = match[2].str();
        conn.password = match[3].str();
        conn.host = match[4].str();
        if (!match[5].str().empty()) {
            conn.port = std::stoi(match[5].str());
        }
        conn.database = match[6].str();
    } else {
        // Simple format: host:port/database
        std::regex simple_regex(R"(([^:/]+)(?::(\d+))?(?:/(.+))?)");
        if (std::regex_match(conn_string, match, simple_regex)) {
            conn.host = match[1].str();
            if (!match[2].str().empty()) {
                conn.port = std::stoi(match[2].str());
            }
            conn.database = match[3].str();
        } else {
            // Just treat as path/database name
            conn.database = conn_string;
            conn.host = "localhost";
        }
    }

    // Set default ports
    if (conn.port == 0) {
        if (type == "postgresql") conn.port = 5432;
        else if (type == "mysql") conn.port = 3306;
    }

    return conn;
}

// ============================================================================
// SQLiteProvider Implementation
// ============================================================================

SQLiteProvider::SQLiteProvider() : db_(nullptr) {
}

SQLiteProvider::~SQLiteProvider() {
    disconnect();
}

bool SQLiteProvider::connect(const DBConnection& conn) {
    if (db_) {
        disconnect();
    }

    db_path_ = conn.path.empty() ? conn.database : conn.path;

    int rc = sqlite3_open(db_path_.c_str(), reinterpret_cast<sqlite3**>(&db_));
    if (rc != SQLITE_OK) {
        std::cerr << "SQLite connect error: " << sqlite3_errmsg(static_cast<sqlite3*>(db_)) << std::endl;
        db_ = nullptr;
        return false;
    }

    return true;
}

void SQLiteProvider::disconnect() {
    if (db_) {
        sqlite3_close(static_cast<sqlite3*>(db_));
        db_ = nullptr;
    }
}

bool SQLiteProvider::isConnected() const {
    return db_ != nullptr;
}

DBResult SQLiteProvider::query(const std::string& sql) {
    DBResult result;
    result.success = false;
    result.affected_rows = 0;
    result.last_insert_id = 0;

    if (!db_) {
        result.error = "Not connected to database";
        return result;
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql.c_str(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        result.error = sqlite3_errmsg(static_cast<sqlite3*>(db_));
        return result;
    }

    // Get column names
    int col_count = sqlite3_column_count(stmt);
    for (int i = 0; i < col_count; i++) {
        result.columns.push_back(sqlite3_column_name(stmt, i));
    }

    // Fetch rows
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DBRow row;
        for (int i = 0; i < col_count; i++) {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            row[result.columns[i]] = value ? value : "";
        }
        result.rows.push_back(row);
    }

    sqlite3_finalize(stmt);
    result.success = true;
    return result;
}

DBResult SQLiteProvider::execute(const std::string& sql) {
    DBResult result;
    result.success = false;
    result.affected_rows = 0;
    result.last_insert_id = 0;

    if (!db_) {
        result.error = "Not connected to database";
        return result;
    }

    char* err_msg = nullptr;
    int rc = sqlite3_exec(static_cast<sqlite3*>(db_), sql.c_str(), nullptr, nullptr, &err_msg);

    if (rc != SQLITE_OK) {
        result.error = err_msg;
        sqlite3_free(err_msg);
        return result;
    }

    result.affected_rows = sqlite3_changes(static_cast<sqlite3*>(db_));
    result.last_insert_id = sqlite3_last_insert_rowid(static_cast<sqlite3*>(db_));
    result.success = true;
    return result;
}

std::vector<std::string> SQLiteProvider::getTables() {
    std::vector<std::string> tables;
    DBResult result = query("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name");

    if (result.success) {
        for (const auto& row : result.rows) {
            auto it = row.find("name");
            if (it != row.end() && it->second != "sqlite_sequence") {
                tables.push_back(it->second);
            }
        }
    }

    return tables;
}

std::vector<DBTable> SQLiteProvider::getSchema() {
    std::vector<DBTable> schema;
    auto tables = getTables();

    for (const auto& table_name : tables) {
        DBTable table;
        table.name = table_name;

        DBResult result = query("PRAGMA table_info(" + table_name + ")");
        if (result.success) {
            for (const auto& row : result.rows) {
                DBColumn col;
                col.name = row.at("name");
                col.type = row.at("type");
                col.nullable = row.at("notnull") == "0";
                col.primary_key = row.at("pk") == "1";
                col.default_value = row.count("dflt_value") ? row.at("dflt_value") : "";
                table.columns.push_back(col);
            }
        }

        schema.push_back(table);
    }

    return schema;
}

std::string SQLiteProvider::escape(const std::string& value) {
    std::string escaped;
    for (char c : value) {
        if (c == '\'') {
            escaped += "''";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

// ============================================================================
// PostgreSQL Provider Implementation
// ============================================================================

#ifdef HAVE_POSTGRESQL
PostgreSQLProvider::PostgreSQLProvider() : conn_(nullptr) {
}

PostgreSQLProvider::~PostgreSQLProvider() {
    disconnect();
}

bool PostgreSQLProvider::connect(const DBConnection& conn) {
    if (conn_) {
        disconnect();
    }

    std::string conninfo = "host=" + conn.host +
                          " port=" + std::to_string(conn.port) +
                          " dbname=" + conn.database;
    if (!conn.username.empty()) {
        conninfo += " user=" + conn.username;
    }
    if (!conn.password.empty()) {
        conninfo += " password=" + conn.password;
    }

    conn_ = PQconnectdb(conninfo.c_str());

    if (PQstatus(static_cast<PGconn*>(conn_)) != CONNECTION_OK) {
        std::cerr << "PostgreSQL connect error: " << PQerrorMessage(static_cast<PGconn*>(conn_)) << std::endl;
        PQfinish(static_cast<PGconn*>(conn_));
        conn_ = nullptr;
        return false;
    }

    return true;
}

void PostgreSQLProvider::disconnect() {
    if (conn_) {
        PQfinish(static_cast<PGconn*>(conn_));
        conn_ = nullptr;
    }
}

bool PostgreSQLProvider::isConnected() const {
    return conn_ != nullptr && PQstatus(static_cast<PGconn*>(conn_)) == CONNECTION_OK;
}

DBResult PostgreSQLProvider::query(const std::string& sql) {
    DBResult result;
    result.success = false;
    result.affected_rows = 0;
    result.last_insert_id = 0;

    if (!conn_) {
        result.error = "Not connected to database";
        return result;
    }

    PGresult* res = PQexec(static_cast<PGconn*>(conn_), sql.c_str());

    if (PQresultStatus(res) != PGRES_TUPLES_OK && PQresultStatus(res) != PGRES_COMMAND_OK) {
        result.error = PQerrorMessage(static_cast<PGconn*>(conn_));
        PQclear(res);
        return result;
    }

    int col_count = PQnfields(res);
    for (int i = 0; i < col_count; i++) {
        result.columns.push_back(PQfname(res, i));
    }

    int row_count = PQntuples(res);
    for (int r = 0; r < row_count; r++) {
        DBRow row;
        for (int c = 0; c < col_count; c++) {
            row[result.columns[c]] = PQgetvalue(res, r, c);
        }
        result.rows.push_back(row);
    }

    PQclear(res);
    result.success = true;
    return result;
}

DBResult PostgreSQLProvider::execute(const std::string& sql) {
    DBResult result;
    result.success = false;
    result.affected_rows = 0;
    result.last_insert_id = 0;

    if (!conn_) {
        result.error = "Not connected to database";
        return result;
    }

    PGresult* res = PQexec(static_cast<PGconn*>(conn_), sql.c_str());

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        result.error = PQerrorMessage(static_cast<PGconn*>(conn_));
        PQclear(res);
        return result;
    }

    const char* affected = PQcmdTuples(res);
    if (affected && *affected) {
        result.affected_rows = std::stoi(affected);
    }

    PQclear(res);
    result.success = true;
    return result;
}

std::vector<std::string> PostgreSQLProvider::getTables() {
    std::vector<std::string> tables;
    DBResult result = query(
        "SELECT table_name FROM information_schema.tables "
        "WHERE table_schema = 'public' ORDER BY table_name"
    );

    if (result.success) {
        for (const auto& row : result.rows) {
            auto it = row.find("table_name");
            if (it != row.end()) {
                tables.push_back(it->second);
            }
        }
    }

    return tables;
}

std::vector<DBTable> PostgreSQLProvider::getSchema() {
    std::vector<DBTable> schema;
    auto tables = getTables();

    for (const auto& table_name : tables) {
        DBTable table;
        table.name = table_name;

        DBResult result = query(
            "SELECT column_name, data_type, is_nullable, column_default "
            "FROM information_schema.columns "
            "WHERE table_name = '" + escape(table_name) + "'"
        );

        if (result.success) {
            for (const auto& row : result.rows) {
                DBColumn col;
                col.name = row.at("column_name");
                col.type = row.at("data_type");
                col.nullable = row.at("is_nullable") == "YES";
                col.primary_key = false;  // Would need separate query
                col.default_value = row.count("column_default") ? row.at("column_default") : "";
                table.columns.push_back(col);
            }
        }

        schema.push_back(table);
    }

    return schema;
}

std::string PostgreSQLProvider::escape(const std::string& value) {
    if (!conn_) return value;
    char* escaped = PQescapeLiteral(static_cast<PGconn*>(conn_), value.c_str(), value.length());
    if (!escaped) return value;
    std::string result(escaped);
    PQfreemem(escaped);
    // Remove surrounding quotes added by PQescapeLiteral
    if (result.size() >= 2 && result.front() == '\'' && result.back() == '\'') {
        result = result.substr(1, result.size() - 2);
    }
    return result;
}
#endif

// ============================================================================
// MySQL Provider Implementation
// ============================================================================

#ifdef HAVE_MYSQL
MySQLProvider::MySQLProvider() : conn_(nullptr) {
}

MySQLProvider::~MySQLProvider() {
    disconnect();
}

bool MySQLProvider::connect(const DBConnection& conn) {
    if (conn_) {
        disconnect();
    }

    conn_ = mysql_init(nullptr);
    if (!conn_) {
        return false;
    }

    MYSQL* result = mysql_real_connect(
        static_cast<MYSQL*>(conn_),
        conn.host.c_str(),
        conn.username.c_str(),
        conn.password.c_str(),
        conn.database.c_str(),
        conn.port,
        nullptr,
        0
    );

    if (!result) {
        std::cerr << "MySQL connect error: " << mysql_error(static_cast<MYSQL*>(conn_)) << std::endl;
        mysql_close(static_cast<MYSQL*>(conn_));
        conn_ = nullptr;
        return false;
    }

    return true;
}

void MySQLProvider::disconnect() {
    if (conn_) {
        mysql_close(static_cast<MYSQL*>(conn_));
        conn_ = nullptr;
    }
}

bool MySQLProvider::isConnected() const {
    return conn_ != nullptr;
}

DBResult MySQLProvider::query(const std::string& sql) {
    DBResult result;
    result.success = false;
    result.affected_rows = 0;
    result.last_insert_id = 0;

    if (!conn_) {
        result.error = "Not connected to database";
        return result;
    }

    if (mysql_query(static_cast<MYSQL*>(conn_), sql.c_str()) != 0) {
        result.error = mysql_error(static_cast<MYSQL*>(conn_));
        return result;
    }

    MYSQL_RES* res = mysql_store_result(static_cast<MYSQL*>(conn_));
    if (!res) {
        // Check if this was a non-SELECT query
        if (mysql_field_count(static_cast<MYSQL*>(conn_)) == 0) {
            result.affected_rows = mysql_affected_rows(static_cast<MYSQL*>(conn_));
            result.success = true;
            return result;
        }
        result.error = mysql_error(static_cast<MYSQL*>(conn_));
        return result;
    }

    int col_count = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);

    for (int i = 0; i < col_count; i++) {
        result.columns.push_back(fields[i].name);
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        DBRow db_row;
        for (int i = 0; i < col_count; i++) {
            db_row[result.columns[i]] = row[i] ? row[i] : "";
        }
        result.rows.push_back(db_row);
    }

    mysql_free_result(res);
    result.success = true;
    return result;
}

DBResult MySQLProvider::execute(const std::string& sql) {
    DBResult result;
    result.success = false;
    result.affected_rows = 0;
    result.last_insert_id = 0;

    if (!conn_) {
        result.error = "Not connected to database";
        return result;
    }

    if (mysql_query(static_cast<MYSQL*>(conn_), sql.c_str()) != 0) {
        result.error = mysql_error(static_cast<MYSQL*>(conn_));
        return result;
    }

    result.affected_rows = mysql_affected_rows(static_cast<MYSQL*>(conn_));
    result.last_insert_id = mysql_insert_id(static_cast<MYSQL*>(conn_));
    result.success = true;
    return result;
}

std::vector<std::string> MySQLProvider::getTables() {
    std::vector<std::string> tables;
    DBResult result = query("SHOW TABLES");

    if (result.success && !result.columns.empty()) {
        for (const auto& row : result.rows) {
            auto it = row.find(result.columns[0]);
            if (it != row.end()) {
                tables.push_back(it->second);
            }
        }
    }

    return tables;
}

std::vector<DBTable> MySQLProvider::getSchema() {
    std::vector<DBTable> schema;
    auto tables = getTables();

    for (const auto& table_name : tables) {
        DBTable table;
        table.name = table_name;

        DBResult result = query("DESCRIBE " + table_name);
        if (result.success) {
            for (const auto& row : result.rows) {
                DBColumn col;
                col.name = row.at("Field");
                col.type = row.at("Type");
                col.nullable = row.at("Null") == "YES";
                col.primary_key = row.at("Key") == "PRI";
                col.default_value = row.count("Default") ? row.at("Default") : "";
                table.columns.push_back(col);
            }
        }

        schema.push_back(table);
    }

    return schema;
}

std::string MySQLProvider::escape(const std::string& value) {
    if (!conn_) return value;
    std::vector<char> escaped(value.length() * 2 + 1);
    mysql_real_escape_string(static_cast<MYSQL*>(conn_), escaped.data(), value.c_str(), value.length());
    return std::string(escaped.data());
}
#endif

// ============================================================================
// DBClient Implementation
// ============================================================================

DBClient::DBClient() : connected_(false) {
}

DBClient::~DBClient() {
    disconnect();
}

bool DBClient::connect(const std::string& type, const std::string& connection_string) {
    disconnect();

    connection_ = DBConnection::parse(type, connection_string);
    connection_.type = type;

    if (type == "sqlite") {
        provider_ = std::make_unique<SQLiteProvider>();
    }
#ifdef HAVE_POSTGRESQL
    else if (type == "postgresql" || type == "postgres") {
        provider_ = std::make_unique<PostgreSQLProvider>();
    }
#endif
#ifdef HAVE_MYSQL
    else if (type == "mysql" || type == "mariadb") {
        provider_ = std::make_unique<MySQLProvider>();
    }
#endif
    else {
        std::cerr << "Unknown database type: " << type << std::endl;
        return false;
    }

    connected_ = provider_->connect(connection_);
    return connected_;
}

void DBClient::disconnect() {
    if (provider_) {
        provider_->disconnect();
        provider_.reset();
    }
    connected_ = false;
}

bool DBClient::isConnected() const {
    return connected_ && provider_ && provider_->isConnected();
}

std::string DBClient::getConnectionType() const {
    return connection_.type;
}

std::string DBClient::getConnectionInfo() const {
    if (connection_.type == "sqlite") {
        return connection_.path.empty() ? connection_.database : connection_.path;
    }
    return connection_.host + ":" + std::to_string(connection_.port) + "/" + connection_.database;
}

DBResult DBClient::query(const std::string& sql) {
    DBResult result;
    result.success = false;

    if (!isConnected()) {
        result.error = "Not connected to database";
        return result;
    }

    return provider_->query(sql);
}

DBResult DBClient::execute(const std::string& sql) {
    DBResult result;
    result.success = false;

    if (!isConnected()) {
        result.error = "Not connected to database";
        return result;
    }

    return provider_->execute(sql);
}

std::vector<DBTable> DBClient::getSchema() {
    if (!isConnected()) return {};
    return provider_->getSchema();
}

std::vector<std::string> DBClient::getTables() {
    if (!isConnected()) return {};
    return provider_->getTables();
}

DBTable DBClient::getTableSchema(const std::string& table) {
    auto schema = getSchema();
    for (const auto& t : schema) {
        if (t.name == table) {
            return t;
        }
    }
    return DBTable{};
}

std::string DBClient::escape(const std::string& value) {
    if (!provider_) return value;
    return provider_->escape(value);
}

bool DBClient::isSafeQuery(const std::string& sql) {
    // Check if query is read-only (SELECT only)
    std::string upper = sql;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    // Trim leading whitespace
    size_t start = upper.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return false;
    upper = upper.substr(start);

    // Safe commands: SELECT, SHOW, DESCRIBE, EXPLAIN, PRAGMA
    return upper.find("SELECT") == 0 ||
           upper.find("SHOW") == 0 ||
           upper.find("DESCRIBE") == 0 ||
           upper.find("EXPLAIN") == 0 ||
           upper.find("PRAGMA") == 0;
}

std::vector<std::string> DBClient::getAvailableTypes() {
    std::vector<std::string> types = {"sqlite"};

#ifdef HAVE_POSTGRESQL
    types.push_back("postgresql");
#endif

#ifdef HAVE_MYSQL
    types.push_back("mysql");
#endif

    return types;
}

} // namespace oleg
