#include "config_file_reader.h"

#include <fstream>

ConfigFileReader::ConfigFileReader(std::string _file_path) {
    std::ifstream is(_file_path);
    std::string temp;
    std::string::iterator it;
    std::string key;

    config_map_.clear();

    while (std::getline(is, temp)) {
        it = temp.begin();

        if (*it == '#') continue;

        for (; it != temp.end(); it++) {
            if (*it == ' ') {
                if (!key.empty()) {
					std::string value = temp.substr(it - temp.begin(), std::string::npos);
					Trim(value);
                    config_map_.insert(std::make_pair(key, value));
                    key.clear();
                    break;
                } else {
                    continue;
                }
            } else {
                key.append(1, *it);
            }
        }
    }
    is.close();
}

ConfigFileReader::ConfigFileReader() {

}

ConfigFileReader::~ConfigFileReader() {

}

void ConfigFileReader::Trim(std::string &str) {
    std::string::iterator it;
    std::string temp;
    auto begin=str.begin();
    auto end=str.begin();
    for (it = str.begin(); it != str.end(); it++) {
        if (*it == ' ' || *it == '\r' || *it == '\n' || *it == '\t')
            continue;
        begin=it;
        while(*it){
            if(*it!=' ' && *it!='\t' && *it!='\n'){
                end=it++;
                continue;
            }
            it++;
        }
        break;
    }
    str=std::string(begin,end+1);
}

int ConfigFileReader::ReadInt(std::string _key) {
    auto it = config_map_.find(_key);
    if (it != config_map_.end()) {
        std::string value = it->second;
        Trim(value);
        return std::stoi(value);
    }
    return 0;
}

std::string ConfigFileReader::ReadString(std::string _key) {
    auto it = config_map_.find(_key);
    if (it != config_map_.end()) {
        std::string value = it->second;
        Trim(value);
        return value;
    }
    return "";
}

std::pair<MIT,MIT> ConfigFileReader::equal(std::string _key) {
	return  config_map_.equal_range(_key);
}
