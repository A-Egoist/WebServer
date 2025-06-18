#include "MySQLConnector.hpp"

MySQLConnector::MySQLConnector() {
    try
    {
        driver_ = get_driver_instance();
        conn_ = driver_->connect("tcp://127.0.0.1:3306", "root", "Lx@259416");
        conn_->setSchema("WebServer_DB");
        Logger::getInstance().log("INFO", "MySQL connection successful!");
    }
    catch(sql::SQLException& e)
    {
        std::cerr << "MySQL Connector /C++ error:" << std::endl;
        std::cerr << "Error code: " << e.getErrorCode() << std::endl;
        std::cerr << "SQLState" << e.getSQLState() << std::endl;
        std::cerr << "Message" << e.what() << std::endl;
    }
}

MySQLConnector::MySQLConnector(const std::string& host, const std::string& sql_user, const std::string& password, const std::string& dbname, unsigned int port) {
    try
    {
        driver_ = get_driver_instance();
        conn_ = driver_->connect("tcp://127.0.0.1:3306", "root", "Lx@259416");
        conn_->setSchema("WebServer_DB");
        Logger::getInstance().log("INFO", "MySQL connection successful!");
    }
    catch(sql::SQLException& e)
    {
        std::cerr << "MySQL Connector /C++ error:" << std::endl;
        std::cerr << "Error code: " << e.getErrorCode() << std::endl;
        std::cerr << "SQLState" << e.getSQLState() << std::endl;
        std::cerr << "Message" << e.what() << std::endl;
    }
}

MySQLConnector::~MySQLConnector() {
    if (conn_) {
        delete conn_;
        conn_ = nullptr;
        // 不删除 driver_，因为它是单例对象
    }
}

bool MySQLConnector::insertUser(const std::string& username, const std::string& password) {
    try {
        sql::PreparedStatement* pstmt = conn_->prepareStatement("INSERT INTO user (username, password) VALUES (?, ?)");
        pstmt->setString(1, username);
        pstmt->setString(2, password);
        pstmt->executeUpdate();
        delete pstmt;
        return true;
    }
    catch(sql::SQLException& e) {
        std::cerr << "Insert failed: " << e.what() << std::endl;
        return false;
    }
}

bool MySQLConnector::verifyUser(const std::string& username, const std::string& password) {
    try {
        sql::PreparedStatement* pstmt = conn_->prepareStatement("SELECT password FROM user WHERE username = ?");
        pstmt->setString(1, username);
        sql::ResultSet* res = pstmt->executeQuery();
        bool isValid = false;
        if (res->next()) {
            std::string storedPassword = res->getString("password");
            isValid = (password == storedPassword);
        }

        delete res;
        delete pstmt;

        return isValid;
    }
    catch(sql::SQLException& e) {
        std::cerr << "Verification failed: " << e.what() << std::endl;
        return false;
    }
}
