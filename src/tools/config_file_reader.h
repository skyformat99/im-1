/*
 * 2017.04.07
 * lngwu11@qq.com
 */

#ifndef _CONFIG_FILE_READER_H
#define _CONFIG_FILE_READER_H

#include <string>
#include <map>

// public.conf
#define CONF_LOG          "../log.conf"
#define CONF_PUBLIC_URL   "../public.conf"
#define CONF_ROUTE_URL   "./route.conf"
#define CONF_REDIS_IP     "redis_ip"
#define CONF_REDIS_PORT   "redis_port"
#define CONF_REDIS_AUTH   "redis_auth"
#define CONF_REDIS_EXPIRE "redis_expire"
#define CONF_ROUTE_IP     "route_ip"
#define CONF_ROUTE_PORT   "route_port"

//loadbalance
#define CONF_LOADBALANCE_IP     "loadbalance_ip"
#define CONF_LOADBALANCE_PORT   "loadbalance_port"

#define CONF_LOADBALANCE_HTTP_IP     "loadbalance_http_ip"
#define CONF_LOADBALANCE_HTTP_PORT   "loadbalance__http_port"

//broadcast

#define CONF_BROADCAST_HTTP_IP     "broadcast_http_ip"
#define CONF_BROADCAST_HTTP_PORT   "broadcast_http_port"


#define CONF_TRANSFER_IP     "transfer_ip"
#define CONF_TRANSFER_PORT   "transfer_port"

// service_com.conf
#define CONF_COM_URL      "./service_com.conf"
#define CONF_COM_IP       "com_ip"
#define CONF_COM_PORT     "com_port"

typedef std::multimap<std::string, std::string>::iterator  MIT;
class ConfigFileReader {
	
public:
    ConfigFileReader(std::string _file_path);
    ConfigFileReader();
    ~ConfigFileReader();

    int ReadInt(std::string _key);
    std::string ReadString(std::string _key);

	std::pair<MIT, MIT> equal(std::string _key);

private:
	
    std::multimap<std::string, std::string> config_map_;

    void Trim(std::string &str);
};


#endif
