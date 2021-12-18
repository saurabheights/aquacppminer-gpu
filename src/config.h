#pragma once
#include <string>

bool configFileExists();
bool loadConfigFile(std::string& log);
bool createConfigFile(std::string& log);
