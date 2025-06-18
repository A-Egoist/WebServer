#pragma once
#include <string>
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/statement.h>
#include <cppconn/resultset.h>
#include <iostream>



class MySQLConnector {
public:
    MySQLConnector();
    MySQLConnector(const std::string&, const std::string&, const std::string&, const std::string&, unsigned int);
    ~MySQLConnector();

    bool insertUser(const std::string&, const std::string&);
    bool verifyUser(const std::string&, const std::string&);
private:
    sql::Driver* driver_;  // 保存 driver 实例
    sql::Connection* conn_;  // 使用 Connector//C++ 的 Connection 对象
};