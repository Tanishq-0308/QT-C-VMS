moc api/company_setting/CompanyController.hpp -o moc_CompanyController.cpp

g++ \
  api/company_setting/test_company.cpp \
  api/company_setting/CompanyController.cpp \
  database/DatabaseManager.cpp \
  moc_CompanyController.cpp \
  moc_DatabaseManager.cpp \
  -Iapi/company_setting -Idatabase -I. \
  -fPIC -std=c++17 -o test_company \
  $(pkg-config --cflags --libs Qt5Core Qt5Sql)
