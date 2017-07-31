#include "pdu_util.h"
#include "log_util.h"
#include <arpa/inet.h>
#include <string.h>
#include <deleter.h>
PduUtil::PduUtil() {

}

/*
 * PDU解析
 */
int PduUtil::OnPduParse(const char *_buf, int _length, PDUBase &_base/*this is return value*/) {
    if (_length < HEAD_LEN) return 0;

    const char *buf = _buf;

    int *startflag = (int*)buf;
    if (ntohl(*startflag) != PDUBase::startflag) {
        LOGE("ntohl(*startflag) != PDUBase::startflag, %d", ntohl(*startflag));
        return -1;
    }
    buf += sizeof(int);

    int *terminal_token = (int*)buf;
    _base.terminal_token = ntohl(*terminal_token);
    buf += sizeof(int);

    int *command_id = (int*)buf;
    _base.command_id = ntohl(*command_id);
    buf += sizeof(int);

    int *seqid = (int*)buf;
    _base.seq_id = ntohl(*seqid);
    buf += sizeof(int);

    char *data_type = (char*)buf;
    _base.data_type_ = *data_type;
    buf += sizeof(char);

    char *version = (char*)buf;
    _base.pdu_version_ = *version;
    buf += sizeof(char);

    char *extension = (char*)buf;
    memcpy(_base.extension_reserved_, extension, sizeof(_base.extension_reserved_));
    buf += sizeof(_base.extension_reserved_);

    int *length = (int*)buf;
    _base.length = ntohl(*length);
    buf += sizeof(int);

    //LOGD("before print length %d", buf - _buf);
    //LOGD("数据全部长度%d", _base.length);

    if (buf - _buf + _base.length > _length) {
        //LOGE("not a full pack, %d + %d > %d", buf - _buf, _base.length, _length);
        return 0;
    }
    std::shared_ptr<char> pbody(new char[_base.length], carray_deleter);
    memcpy(pbody.get(), buf, _base.length);
    _base.body = pbody;
    return buf - _buf + _base.length;
}
int PduUtil::_OnPduParse(const char *_buf, int _length, PDUBase &_base/*this is return value*/) {
    if (_length <= 0) return 0;

    const char *buf = _buf;

    int *startflag = (int*)buf;
    if (ntohl(*startflag) != PDUBase::startflag) {
        LOGE("ntohl(*startflag) != PDUBase::startflag, %d", ntohl(*startflag));
        return -1;
    }
    buf += sizeof(int);

    int *terminal_token = (int*)buf;
    _base.terminal_token = ntohl(*terminal_token);
    buf += sizeof(int);

    int *command_id = (int*)buf;
    _base.command_id = ntohl(*command_id);
    buf += sizeof(int);

    int *seqid = (int*)buf;
    _base.seq_id = ntohl(*seqid);
    buf += sizeof(int);

    char *data_type = (char*)buf;
    _base.data_type_ = *data_type;
    buf += sizeof(char);

    char *version = (char*)buf;
    _base.pdu_version_ = *version;
    buf += sizeof(char);

    char *extension = (char*)buf;
    memcpy(_base.extension_reserved_, extension, sizeof(_base.extension_reserved_));
    buf += sizeof(_base.extension_reserved_);

    int *length = (int*)buf;
    _base.length = ntohl(*length);
    buf += sizeof(int);

    //LOGD("before print length %d", buf - _buf);
    //LOGD("数据全部长度%d", _base.length);

  /*  if (buf - _buf + _base.length > _length) {
        //LOGE("not a full pack, %d + %d > %d", buf - _buf, _base.length, _length);
        return 0;
    }*/
    std::shared_ptr<char> pbody(new char[_base.length], carray_deleter);
    memcpy(pbody.get(), buf, _length-(buf-_buf));
    _base.body = pbody;
	return 0;
 //   return buf - _buf + _base.length;
}

/*
 * PDU封包
 */
int PduUtil::OnPduPack(PDUBase &_base, std::shared_ptr<char> &_outbuf/*this is return value*/) {
    int total_len = 0;

    int startflag = htonl(PDUBase::startflag);
    total_len += sizeof(int);

    int terminal_token = htonl(_base.terminal_token);
    total_len += sizeof(int);

    int commandid = htonl(_base.command_id);
    total_len += sizeof(int);

    int seq = htonl( _base.seq_id);
    total_len += sizeof(int);

    total_len += sizeof(char);
    total_len += sizeof(char);
    total_len += sizeof(_base.extension_reserved_);

    int proto_len = htonl(_base.length);
    total_len += sizeof(int);

    total_len += _base.length;
    std::shared_ptr<char> sp_buf(new char[total_len], carray_deleter);
    char *buf = sp_buf.get();

    int offset = 0;
    memcpy(buf + offset, (char*)(&startflag), sizeof(int));
    offset += sizeof(int);

    memcpy(buf + offset, (char*)(&terminal_token), sizeof(int));
    offset += sizeof(int);

    memcpy(buf + offset, (char*)(&commandid), sizeof(int));
    offset += sizeof(int);

    memcpy(buf + offset, (char*)(&seq), sizeof(int));
    offset += sizeof(int);

    memcpy(buf + offset, (char*)(&(_base.data_type_)), sizeof(_base.data_type_));
    offset += sizeof(_base.data_type_);

    memcpy(buf + offset, (char*)(&(_base.pdu_version_)), sizeof(_base.pdu_version_));
    offset += sizeof(_base.pdu_version_);

    memcpy(buf + offset, (char*)(&(_base.extension_reserved_)), sizeof(_base.extension_reserved_));
    offset += sizeof(_base.extension_reserved_);

    memcpy(buf + offset, (char*)(&proto_len), sizeof(int));
    offset += sizeof(int);

    memcpy(buf + offset, _base.body.get(), _base.length);

    _outbuf = sp_buf;
    return total_len;
}

/*
* PDU封包
*/
int PduUtil::OnPduPack(PDUBase &_base, char*& _outbuf/*this is return value*/) {
	int total_len = 0;

	int startflag = htonl(PDUBase::startflag);
	total_len += sizeof(int);

	int terminal_token = htonl(_base.terminal_token);
	total_len += sizeof(int);

	int commandid = htonl(_base.command_id);
	total_len += sizeof(int);

	int seq = htonl(_base.seq_id);
	total_len += sizeof(int);

	total_len += sizeof(char);
	total_len += sizeof(char);
	total_len += sizeof(_base.extension_reserved_);

	int proto_len = htonl(_base.length);
	total_len += sizeof(int);

	total_len += _base.length;

	char *buf = new char[total_len];

	int offset = 0;
	memcpy(buf + offset, (char*)(&startflag), sizeof(int));
	offset += sizeof(int);

	memcpy(buf + offset, (char*)(&terminal_token), sizeof(int));
	offset += sizeof(int);

	memcpy(buf + offset, (char*)(&commandid), sizeof(int));
	offset += sizeof(int);

	memcpy(buf + offset, (char*)(&seq), sizeof(int));
	offset += sizeof(int);

	memcpy(buf + offset, (char*)(&(_base.data_type_)), sizeof(_base.data_type_));
	offset += sizeof(_base.data_type_);

	memcpy(buf + offset, (char*)(&(_base.pdu_version_)), sizeof(_base.pdu_version_));
	offset += sizeof(_base.pdu_version_);

	memcpy(buf + offset, (char*)(&(_base.extension_reserved_)), sizeof(_base.extension_reserved_));
	offset += sizeof(_base.extension_reserved_);

	memcpy(buf + offset, (char*)(&proto_len), sizeof(int));
	offset += sizeof(int);

	memcpy(buf + offset, _base.body.get(), _base.length);

	_outbuf = buf;
	return total_len;
}
