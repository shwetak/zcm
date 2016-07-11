%module zcmswig
%{
#include "zcm/zcm-cpp.hpp"
void handler_cb(const zcm_recv_buf_t *rbuf, const char *channel, void *usr);
%}
%feature("director") zcm::Subscription;
%include "stdint.i"
%include "std_string.i"
%include "std_vector.i"
%include "typemaps.i"
#include "zcm/zcm-cpp.hpp"

void handler_cb(const zcm_recv_buf_t *rbuf, const char *channel, void *usr) {
    subs = (<ZCMSubscription>usr)
    msg = subs.msgtype.decode(rbuf.data[:rbuf.data_size])
    subs.handler(channel, msg)
}

%pythoncode {

    def subscribe(zcm, channel, msgtype, handler):
        class ZCMSubscription:
            def __init__(self):
                self.msgtype = None
                self.handler = None
                self.subs    = None

        subs = ZCMSubscription()
        subs.msgtype = msgtype
        subs.handler = handler
        subs.sub = zcm.subscribe(channel, handler_cb, subs)
        return subs
%}
