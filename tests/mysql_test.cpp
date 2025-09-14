#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

int main() {
    try {
        sql::Driver* driver = get_driver_instance();
        sql::Connection* con = driver->connect("tcp://127.0.0.1:3306", "root", "Lx@259416");
        con->setSchema("WebServer_DB");
        std::cout << "MySQL 连接成功！" << std::endl;
        delete con;
    } catch (sql::SQLException& e) {
        std::cerr << "MySQL 错误：" << e.what() << std::endl;
    }
    return 0;
}