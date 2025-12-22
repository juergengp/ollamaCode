#ifndef OLEG_DB_CLIENT_H
#define OLEG_DB_CLIENT_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

namespace oleg {

// Query result row
using DBRow = std::map<std::string, std::string>;

// Query result set
struct DBResult {
    bool success;
    std::string error;
    std::vector<std::string> columns;
    std::vector<DBRow> rows;
    int affected_rows;
    long long last_insert_id;
};

// Table schema info
struct DBColumn {
    std::string name;
    std::string type;
    bool nullable;
    bool primary_key;
    std::string default_value;
};

struct DBTable {
    std::string name;
    std::vector<DBColumn> columns;
};

// Database connection info
struct DBConnection {
    std::string type;      // "sqlite", "postgresql", "mysql"
    std::string host;
    int port;
    std::string database;
    std::string username;
    std::string password;
    std::string path;      // For SQLite file path

    // Parse connection string
    static DBConnection parse(const std::string& type, const std::string& conn_string);
};

// Database provider interface
class DBProvider {
public:
    virtual ~DBProvider() = default;

    virtual bool connect(const DBConnection& conn) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual DBResult query(const std::string& sql) = 0;
    virtual DBResult execute(const std::string& sql) = 0;

    virtual std::vector<DBTable> getSchema() = 0;
    virtual std::vector<std::string> getTables() = 0;

    virtual std::string getName() const = 0;
    virtual std::string escape(const std::string& value) = 0;
};

// SQLite provider
class SQLiteProvider : public DBProvider {
public:
    SQLiteProvider();
    ~SQLiteProvider() override;

    bool connect(const DBConnection& conn) override;
    void disconnect() override;
    bool isConnected() const override;

    DBResult query(const std::string& sql) override;
    DBResult execute(const std::string& sql) override;

    std::vector<DBTable> getSchema() override;
    std::vector<std::string> getTables() override;

    std::string getName() const override { return "sqlite"; }
    std::string escape(const std::string& value) override;

private:
    void* db_;  // sqlite3*
    std::string db_path_;
};

#ifdef HAVE_POSTGRESQL
// PostgreSQL provider
class PostgreSQLProvider : public DBProvider {
public:
    PostgreSQLProvider();
    ~PostgreSQLProvider() override;

    bool connect(const DBConnection& conn) override;
    void disconnect() override;
    bool isConnected() const override;

    DBResult query(const std::string& sql) override;
    DBResult execute(const std::string& sql) override;

    std::vector<DBTable> getSchema() override;
    std::vector<std::string> getTables() override;

    std::string getName() const override { return "postgresql"; }
    std::string escape(const std::string& value) override;

private:
    void* conn_;  // PGconn*
};
#endif

#ifdef HAVE_MYSQL
// MySQL provider
class MySQLProvider : public DBProvider {
public:
    MySQLProvider();
    ~MySQLProvider() override;

    bool connect(const DBConnection& conn) override;
    void disconnect() override;
    bool isConnected() const override;

    DBResult query(const std::string& sql) override;
    DBResult execute(const std::string& sql) override;

    std::vector<DBTable> getSchema() override;
    std::vector<std::string> getTables() override;

    std::string getName() const override { return "mysql"; }
    std::string escape(const std::string& value) override;

private:
    void* conn_;  // MYSQL*
};
#endif

// Main database client
class DBClient {
public:
    DBClient();
    ~DBClient();

    // Connection management
    bool connect(const std::string& type, const std::string& connection_string);
    void disconnect();
    bool isConnected() const;

    // Get connection info
    std::string getConnectionType() const;
    std::string getConnectionInfo() const;

    // Query operations
    DBResult query(const std::string& sql);
    DBResult execute(const std::string& sql);

    // Schema operations
    std::vector<DBTable> getSchema();
    std::vector<std::string> getTables();
    DBTable getTableSchema(const std::string& table);

    // Utility
    std::string escape(const std::string& value);
    bool isSafeQuery(const std::string& sql);

    // Get available database types
    static std::vector<std::string> getAvailableTypes();

private:
    std::unique_ptr<DBProvider> provider_;
    DBConnection connection_;
    bool connected_;
};

} // namespace oleg

#endif // OLEG_DB_CLIENT_H
