#include <core.p4>
#include <v1model.p4>

header easyroute_head_t {
    bit<64> preamble;
    bit<32> num_valid;
}

header easyroute_port_t {
    bit<8> port;
}

struct metadata {
}

struct headers {
    @name("easyroute_head") 
    easyroute_head_t easyroute_head;
    @name("easyroute_port") 
    easyroute_port_t easyroute_port;
}

parser ParserImpl(packet_in packet, out headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    bit<64> tmp_0;
    @name("parse_head") state parse_head {
        packet.extract<easyroute_head_t>(hdr.easyroute_head);
        transition select(hdr.easyroute_head.num_valid) {
            32w0: accept;
            default: parse_port;
        }
    }
    @name("parse_port") state parse_port {
        packet.extract<easyroute_port_t>(hdr.easyroute_port);
        transition accept;
    }
    @name("start") state start {
        tmp_0 = packet.lookahead<bit<64>>();
        transition select(tmp_0[63:0]) {
            64w0: parse_head;
            default: accept;
        }
    }
}

control egress(inout headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    apply {
    }
}

control ingress(inout headers hdr, inout metadata meta, inout standard_metadata_t standard_metadata) {
    @name("NoAction") action NoAction_0() {
    }
    @name("._drop") action _drop_0() {
        mark_to_drop();
    }
    @name(".route") action route_0() {
        standard_metadata.egress_spec = (bit<9>)hdr.easyroute_port.port;
        hdr.easyroute_head.num_valid = hdr.easyroute_head.num_valid + 32w4294967295;
        hdr.easyroute_port.setInvalid();
    }
    @name("route_pkt") table route_pkt {
        actions = {
            _drop_0();
            route_0();
            @default_only NoAction_0();
        }
        key = {
            hdr.easyroute_port.isValid(): exact @name("hdr.easyroute_port.isValid()") ;
        }
        size = 1;
        default_action = NoAction_0();
    }
    apply {
        route_pkt.apply();
    }
}

control DeparserImpl(packet_out packet, in headers hdr) {
    apply {
        packet.emit<easyroute_head_t>(hdr.easyroute_head);
        packet.emit<easyroute_port_t>(hdr.easyroute_port);
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
