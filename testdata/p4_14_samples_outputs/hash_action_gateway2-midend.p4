#include <core.p4>
#include <v1model.p4>

struct counter_metadata_t {
    bit<16> counter_index;
    bit<4>  counter_run;
}

header data_t {
    bit<32> f1;
    bit<32> f2;
    bit<16> h1;
    bit<16> h2;
    bit<16> h3;
    bit<16> h4;
}

struct metadata {
    @name("counter_metadata") 
    counter_metadata_t counter_metadata;
}

struct headers {
    @name("data") 
    data_t data;
}

parser ParserImpl(packet_in packet, out headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    @name("start") state start {
        packet.extract<data_t>(hdr.data);
        transition accept;
    }
}

control ingress(inout headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    @name("NoAction") action NoAction_0() {
    }
    @name("NoAction") action NoAction_4() {
    }
    @name("NoAction") action NoAction_5() {
    }
    @name("count1") @min_width(32) counter(32w16384, CounterType.packets) count1;
    @name(".set_index") action set_index_0(bit<16> index, bit<9> port) {
        meta.counter_metadata.counter_index = index;
        standard_metadata.egress_spec = port;
        meta.counter_metadata.counter_run = 4w1;
    }
    @name(".count_entries") action count_entries_0() {
        count1.count((bit<32>)meta.counter_metadata.counter_index);
    }
    @name(".seth2") action seth2_0(bit<16> val) {
        hdr.data.h2 = val;
    }
    @name(".seth4") action seth4_0(bit<16> val) {
        hdr.data.h4 = val;
    }
    @name("index_setter") table index_setter {
        actions = {
            set_index_0();
            @default_only NoAction_0();
        }
        key = {
            hdr.data.f1: exact @name("hdr.data.f1") ;
            hdr.data.f2: exact @name("hdr.data.f2") ;
        }
        size = 2048;
        default_action = NoAction_0();
    }
    @name("stats") table stats {
        actions = {
            count_entries_0();
        }
        const default_action = count_entries_0();
    }
    @name("test1") table test1 {
        actions = {
            seth2_0();
            @default_only NoAction_4();
        }
        key = {
            hdr.data.h1: exact @name("hdr.data.h1") ;
        }
        default_action = NoAction_4();
    }
    @name("test2") table test2 {
        actions = {
            seth4_0();
            @default_only NoAction_5();
        }
        key = {
            hdr.data.h2: exact @name("hdr.data.h2") ;
        }
        default_action = NoAction_5();
    }
    apply {
        index_setter.apply();
        if (meta.counter_metadata.counter_run == 4w1) {
            stats.apply();
            test1.apply();
        }
        else 
            test2.apply();
    }
}

control egress(inout headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    apply {
    }
}

control DeparserImpl(packet_out packet, in headers hdr) {
    apply {
        packet.emit<data_t>(hdr.data);
    }
}

control verifyChecksum(in headers hdr, inout metadata meta) {
    apply {
    }
}

control computeChecksum(inout headers hdr, inout metadata meta) {
    apply {
    }
}

V1Switch<headers, metadata>(ParserImpl(), verifyChecksum(), ingress(), egress(), computeChecksum(), DeparserImpl()) main;
