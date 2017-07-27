#include "pdu_base.h"

PDUBase::PDUBase() {
    this->terminal_token = 0;
    this->length = 0;
    this->seq_id = 0;
    this->command_id = 0;
    this->data_type_ = 0;
    this->pdu_version_ = 0;
}
