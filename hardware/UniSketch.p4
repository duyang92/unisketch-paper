#include<core.p4>
#if __TARGET_TOFINO__ == 2
#include<t2na.p4>
#else
#include<tna.p4>
#endif

// parameter setting
// s = 128
// l = L = 16
// m = 358400

#define L 16
#define layer_width 4587520 // L * m
#define BUCKET_ARRAY_LEN 15360


/* header definitions */
header Ethernet {
	bit<48> dstAddr;
	bit<48> srcAddr;
	bit<16> etherType;
}

header Ipv4{
	bit<4> version;
	bit<4> ihl;
	bit<8> diffserv;
    bit<16> total_len;
	bit<16> identification;
	bit<3> flags;
	bit<13> fragOffset;
	bit<8> ttl;
    bit<8> protocol;
	bit<16> checksum;
	bit<32> srcAddr;
	bit<32> dstAddr;
}

header resubmit_data{
	bit<32> n_f_idx_i; // [31:0] n_f  [4:0] idx_i
	bit<32> case_slot_idx;  // [31:0] c_temp  [1:0] case
}

struct ingress_headers_t{
	Ethernet ethernet;
	Ipv4 ipv4;
}

struct ingress_metadata_t{
	resubmit_data submit_data;
	bit<32> h_v;
	bit<16> index_u;
	bit<16> rng_1;
	bit<16> rng_2;
	bit<20> n_f_n_s1;
	bit<5> index_i;
	bit<32> index_j;
	bit<32> index_M;
	bit<32> read_id_1;
	bit<32> read_counter_1;
	bit<32> read_id_2;
	bit<32> read_counter_2;
	bit<32> c_temp;
	bit<32> bucket_counter;
	bit<32> cond;
	bit<32> ns;
	
	bit<32> gamma;
	bit<1> bit_res;
	bit<8> h_s;

	bit<32> temp1;
	bit<32> temp2;
	bit<32> n_f_n_s;
	bit<1> bucket_idx;

	bit<32> ns_delta;
	bit<32> delta;
}

struct egress_headers_t {}
struct egress_metadata_t {}

enum bit<16> ether_type_t {
    IPV4    = 0x0800,
    ARP     = 0x0806
}

enum bit<8> ip_proto_t {
    ICMP    = 1,
    IGMP    = 2,
    TCP     = 6,
    UDP     = 17
}

/* parser processing */

parser IngressParser(packet_in pkt,
	out ingress_headers_t hdr,
	out ingress_metadata_t metadata,
	out ingress_intrinsic_metadata_t ig_intr_md)
{
	state start{
		pkt.extract(ig_intr_md);
        transition select(ig_intr_md.resubmit_flag) {
            1 : parse_resubmit;
            0 : parse_port_metadata;
        }
	}

    state parse_resubmit {
        pkt.extract(metadata.submit_data);
        transition parse_ethernet;
    }

    state parse_port_metadata {
        pkt.advance(64); // Only apply for Tofino 1
        transition parse_ethernet;
    }

	state parse_ethernet{
		pkt.extract(hdr.ethernet);
        transition select((bit<16>)hdr.ethernet.etherType) {
            (bit<16>)ether_type_t.IPV4      : parse_ipv4;
            (bit<16>)ether_type_t.ARP       : accept;
            default : accept;
        }
	}

	state parse_ipv4{
		pkt.extract(hdr.ipv4);
        transition select(hdr.ipv4.protocol) {
            (bit<8>)ip_proto_t.ICMP             : accept;
            (bit<8>)ip_proto_t.IGMP             : accept;
            (bit<8>)ip_proto_t.TCP              : parse_myflow;
            (bit<8>)ip_proto_t.UDP              : parse_myflow;
            default : accept;
        }
	}

	state parse_myflow{
		transition accept;
	}

}


/* ingress */
@pa_atomic("ingress", "metadata.rng_1")
@pa_atomic("ingress", "metadata.rng_2")
@pa_atomic("ingress", "metadata.cond")
control Ingress(inout ingress_headers_t hdr,
		inout ingress_metadata_t meta,
		in ingress_intrinsic_metadata_t ig_intr_md,
		in ingress_intrinsic_metadata_from_parser_t ig_prsr_md,
		inout ingress_intrinsic_metadata_for_deparser_t ig_dprsr_md,
		inout ingress_intrinsic_metadata_for_tm_t ig_tm_md)
{
	// compute index i

    CRCPolynomial<bit<32>>(coeff=0x04C11DB7,reversed=true, msb=false, extended=false, init=0xFFFFFFFF, xor=0xFFFFFFFF) crc32_1;

	Hash<bit<32>>(HashAlgorithm_t.CUSTOM, crc32_1) H_v;
	
	action compute_h_v(){
		meta.h_v = H_v.get({hdr.ipv4.srcAddr ^ hdr.ipv4.dstAddr});
	}

	action get_index_i(bit<5> prefix) {
		meta.index_i = prefix;
	}

	table cal_index_i {
		key = {
			meta.h_v : lpm;
		}
		actions = {
			get_index_i;
			NoAction;
		}
		// const entries = {
		// 	0x00000000 &&& 32w0x00000000 : get_index_i(32);  // 32 leading zeros
		// 	0x00000000 &&& 32w0x80000000 : get_index_i(31);  // 31 leading zeros
		// 	0x00000000 &&& 32w0xc0000000 : get_index_i(30);  // 30 leading zeros
		// 	0x00000000 &&& 32w0xe0000000 : get_index_i(29);  // 29 leading zeros
		// 	0x00000000 &&& 32w0xf0000000 : get_index_i(28);  // 28 leading zeros
		// 	0x00000000 &&& 32w0xf8000000 : get_index_i(27);  // 27 leading zeros
		// 	0x00000000 &&& 32w0xfc000000 : get_index_i(26);  // 26 leading zeros
		// 	0x00000000 &&& 32w0xfe000000 : get_index_i(25);  // 25 leading zeros
		// 	0x00000000 &&& 32w0xff000000 : get_index_i(24);  // 24 leading zeros
		// 	0x00000000 &&& 32w0xff800000 : get_index_i(23);  // 23 leading zeros
		// 	0x00000000 &&& 32w0xffc00000 : get_index_i(22);  // 22 leading zeros
		// 	0x00000000 &&& 32w0xffe00000 : get_index_i(21);  // 21 leading zeros
		// 	0x00000000 &&& 32w0xfff00000 : get_index_i(20);  // 20 leading zeros
		// 	0x00000000 &&& 32w0xfff80000 : get_index_i(19);  // 19 leading zeros
		// 	0x00000000 &&& 32w0xfffc0000 : get_index_i(18);  // 18 leading zeros
		// 	0x00000000 &&& 32w0xfffe0000 : get_index_i(17);  // 17 leading zeros
		// 	0x00000000 &&& 32w0xffff0000 : get_index_i(16);  // 16 leading zeros
		// 	0x00000000 &&& 32w0xffff8000 : get_index_i(15);  // 15 leading zeros
		// 	0x00000000 &&& 32w0xffffc000 : get_index_i(14);  // 14 leading zeros
		// 	0x00000000 &&& 32w0xffffe000 : get_index_i(13);  // 13 leading zeros
		// 	0x00000000 &&& 32w0xfffff000 : get_index_i(12);  // 12 leading zeros
		// 	0x00000000 &&& 32w0xfffff800 : get_index_i(11);  // 11 leading zeros
		// 	0x00000000 &&& 32w0xfffffc00 : get_index_i(10);  // 10 leading zeros
		// 	0x00000000 &&& 32w0xfffffe00 : get_index_i(9);   // 9 leading zeros
		// 	0x00000000 &&& 32w0xffffff00 : get_index_i(8);   // 8 leading zeros
		// 	0x00000000 &&& 32w0xffffff80 : get_index_i(7);   // 7 leading zeros
		// 	0x00000000 &&& 32w0xffffffc0 : get_index_i(6);   // 6 leading zeros
		// 	0x00000000 &&& 32w0xffffffe0 : get_index_i(5);   // 5 leading zeros
		// 	0x00000000 &&& 32w0xfffffff0 : get_index_i(4);   // 4 leading zeros
		// 	0x00000000 &&& 32w0xfffffff8 : get_index_i(3);   // 3 leading zeros
		// 	0x00000000 &&& 32w0xfffffffc : get_index_i(2);   // 2 leading zeros
		// 	0x00000000 &&& 32w0xfffffffe : get_index_i(1);   // 1 leading zero
		// 	0x00000000 &&& 32w0xffffffff : get_index_i(0);   // 0 leading zero
		// }
	}

	// compute index j

    CRCPolynomial<bit<32>>(coeff=0x04C11DB7,reversed=true, msb=false, extended=false, init=32w0xFFFFFFFF, xor=32w0x00000000) crc32_2;

	Hash<bit<7>>(HashAlgorithm_t.CUSTOM, crc32_2) H_s;
	
	action compute_h_s(){
		meta.h_s = (bit<8>) H_s.get({hdr.ipv4.srcAddr ^ hdr.ipv4.dstAddr});
	}

    CRCPolynomial<bit<32>>(coeff=0x04C11DB7,reversed=true, msb=false, extended=false, init=32w0xFFFFFFFF, xor=32w0x88888888) crc32_3;

	Hash<bit<32>>(HashAlgorithm_t.CUSTOM, crc32_3) H_m;
	
	action compute_index_j(){
		meta.index_j = meta.index_M + H_m.get({hdr.ipv4.srcAddr ^ (bit<32>) meta.h_s});
	}

	// visit M

	Register<bit<1>,bit<32>>(layer_width) M;
	RegisterAction<bit<1>,bit<32>,bit<1>>(M) M_alu={
		void apply(inout bit<1> input_data, out bit<1> output_data){
			output_data = input_data;
			input_data = 1;
		}
	};
	action read_M(){
		meta.bit_res = M_alu.execute(meta.index_j);
	}

	// visit B

	CRCPolynomial<bit<32>>(coeff=0x04C11DB7,reversed=true, msb=false, extended=false, init=32w0xFFFFFFFF, xor=32w0xFFFFFFFF) crc32_4;

	Hash<bit<16>>(HashAlgorithm_t.CUSTOM, crc32_4) H_mm;

	action compute_index_u(){
		meta.index_u = H_mm.get({hdr.ipv4.srcAddr});
	}

	Register<bit<32>,bit<16>>(BUCKET_ARRAY_LEN) B_key_1;
	RegisterAction<bit<32>,bit<16>,bit<32>>(B_key_1) visit_B_key_1={
		void apply(inout bit<32> item_id, out bit<32> output){
			output = item_id;
		}
	};
	action B_key_1_action(){
		meta.read_id_1 = visit_B_key_1.execute(meta.index_u);
	}
	RegisterAction<bit<32>,bit<16>,bit<32>>(B_key_1) modify_B_key_1={
		void apply(inout bit<32> item_id, out bit<32> output){
			item_id = hdr.ipv4.srcAddr;
		}
	};
	action modify_B_key_1_action(){
		modify_B_key_1.execute(meta.index_u);
	}

	Register<bit<32>,bit<16>>(BUCKET_ARRAY_LEN) B_value_1;
	RegisterAction<bit<32>,bit<16>,bit<32>>(B_value_1) visit_B_value_1={
		void apply(inout bit<32> item_value, out bit<32> output){
			output = item_value;
		}
	};
	action B_value_1_action(){
		meta.read_counter_1 = visit_B_value_1.execute(meta.index_u);
	}
	RegisterAction<bit<32>,bit<16>,bit<32>>(B_value_1) modify_B_value_1={
		void apply(inout bit<32> item_value, out bit<32> output){
			item_value = item_value + (bit<32>) meta.submit_data.delta;
		}
	};
	action modify_B_value_1_action(){
		modify_B_value_1.execute(meta.index_u);
	}

	Register<bit<32>,bit<16>>(BUCKET_ARRAY_LEN) B_key_2;
	RegisterAction<bit<32>,bit<16>,bit<32>>(B_key_2) visit_B_key_2={
		void apply(inout bit<32> item_id, out bit<32> output){
			output = item_id;
		}
	};
	action B_key_2_action(){
		meta.read_id_2 = visit_B_key_2.execute(meta.index_u);
	}
	RegisterAction<bit<32>,bit<16>,bit<32>>(B_key_2) modify_B_key_2={
		void apply(inout bit<32> item_id, out bit<32> output){
			item_id = hdr.ipv4.srcAddr;
		}
	};
	action modify_B_key_2_action(){
		modify_B_key_2.execute(meta.index_u);
	}

	Register<bit<32>,bit<16>>(BUCKET_ARRAY_LEN) B_value_2;
	RegisterAction<bit<32>,bit<16>,bit<32>>(B_value_2) visit_B_value_2={
		void apply(inout bit<32> item_value, out bit<32> output){
			output = item_value;
		}
	};
	action B_value_2_action(){
		meta.read_counter_2 = visit_B_value_2.execute(meta.index_u);
	}
	RegisterAction<bit<32>,bit<16>,bit<32>>(B_value_2) modify_B_value_2={
		void apply(inout bit<32> item_value, out bit<32> output){
			item_value = item_value + (bit<32>) meta.submit_data.delta;
		}
	};
	action modify_B_value_2_action(){
		modify_B_value_2.execute(meta.index_u);
	}

	// compute gamma

	action get_gamma(bit<32> gamma) {
		meta.gamma = gamma;
	}
	table cal_gamma {
		key = {
			meta.n_f_n_s1 : range;
		}
		actions = {
			get_gamma;
		}
		const entries = {
			0x00000..0x00054 : get_gamma(0);
			0x00055..0x000aa : get_gamma(1);
			0x000ab..0x00155 : get_gamma(2);
			0x00156..0x002ab : get_gamma(3);
			0x002ac..0x00558 : get_gamma(4);
			0x00559..0x00ab1 : get_gamma(5);
			0x00ab2..0x01564 : get_gamma(6);
			0x01565..0x02ac9 : get_gamma(7);
			0x02aca..0x05593 : get_gamma(8);
			0x05594..0x0ab28 : get_gamma(9);
			0x0ab29..0x15651 : get_gamma(10);
			0x15652..0x2aca4 : get_gamma(11);
		}
	}


	// compute delta

	action get_ns_delta(bit<32> ns_delta) {
		meta.ns_delta = ns_delta;
	}

	table cal_ns_delta {
		key = {
			meta.submit_data.n_f_idx_i [4:0] : exact;
		}
		actions = {
			get_ns_delta;
		}
		const entries = {
			0: get_ns_delta(1);
			1: get_ns_delta(2);
			2: get_ns_delta(4);
			3: get_ns_delta(8);
			4: get_ns_delta(16);
			5: get_ns_delta(32);
			6: get_ns_delta(64);
			7: get_ns_delta(128);
			8: get_ns_delta(256);
			9: get_ns_delta(512);
			10: get_ns_delta(1024);
			11: get_ns_delta(2048);
			12: get_ns_delta(4096);
			13: get_ns_delta(8192);
			14: get_ns_delta(16384);
			15: get_ns_delta(32768);
		}
	}

	Register<bit<32>, _>(1) ns_reg;

	RegisterAction<bit<32>, _, bit<32>>(ns_reg) read_ns_reg = {
		void apply(inout bit<32> value, out bit<32> result) {
			value = value + meta.ns_delta;
			result = value;
		}
	};

	action read_ns(){
		meta.ns = read_ns_reg.execute(0);
	}

	// replace

	Random<bit<16>>() random_generator1;
	Random<bit<16>>() random_generator2;

	action generate_random_number_1(){
		meta.rng_1 = random_generator1.get();
	}

	action generate_random_number_2(){
		meta.rng_2 = random_generator2.get();
	}

	Register<bit<32>,bit<1>>(1) num_32;

	MathUnit<bit<32>>(true,0,9,{68,73,78,85,93,102,113,128,0,0,0,0,0,0,0,0}) prog_64K_div_mu;

	RegisterAction<bit<32>,bit<1>,bit<32>>(num_32) prog_64K_div_x = {
		void apply(inout bit<32> register_data, out bit<32> mau_value){
			register_data = prog_64K_div_mu.execute( (bit<32>) (meta.c_temp[31:10]) );
            mau_value = register_data;
		}
	};

	action calc_cond(){
		meta.cond = (bit<32>)meta.rng_1 - prog_64K_div_x.execute(0);
	}

	action cal1(){
		meta.temp1 = meta.submit_data.n_f_idx_i + meta.ns;
	}

	action cal2(){
		meta.n_f_n_s = (meta.temp1 >> 7);
	}

	action cal3(){
		meta.temp2 = (meta.temp1 >> 8);
	}

	action cal4(){
		meta.n_f_n_s = meta.n_f_n_s + meta.temp2;
	}

	/* ingress processing*/
	apply{
		
		if(ig_intr_md.resubmit_flag == 0) {
			
			// compute index i, j to visit M
			compute_h_v();
			cal_index_i.apply();
			meta.index_M = (bit<32>) (meta.index_i << 27);

			compute_h_s();
			compute_index_j();

			read_M();

			// compute index u to visit B
			compute_index_u();

			B_key_1_action();
			B_value_1_action();
			B_key_2_action();
			B_value_2_action();

			bit<1> step1;
			bit<1> case;

			// locate target bucket slot
			if (meta.read_id_1 == hdr.ipv4.srcAddr || meta.read_id_2 == hdr.ipv4.srcAddr) {
				//case 1
				if (meta.read_id_1 == hdr.ipv4.srcAddr) {
					meta.bucket_idx = 1w0;
					meta.submit_data.n_f_idx_i = meta.read_counter_1;
				}
				else{
					meta.bucket_idx = 1w1;
					meta.submit_data.n_f_idx_i = meta.read_counter_2;
				}
				case = 0;
				ig_dprsr_md.resubmit_type = 2;
				step1 = 1;
			}
			else {
				// case 2
				if (meta.read_id_1 == 0 || meta.read_id_2 == 0){
					if (meta.read_id_1 == 0) {
						meta.bucket_idx = 1w0;
						meta.submit_data.n_f_idx_i = 0;
					}
					else{
						meta.bucket_idx = 1w1;
						meta.submit_data.n_f_idx_i = 0;
					}
					case = 0;
					ig_dprsr_md.resubmit_type = 2;
				}
				else {
					// case 3
					// locate the smallest slot
					if (meta.read_counter_1 - meta.read_counter_2 < 0) {
						meta.bucket_idx = 1w0;
						meta.submit_data.n_f_idx_i = 0;
						case = 1;
						meta.c_temp = meta.read_counter_1;
					}
					else{
						meta.bucket_idx = 1w1;
						meta.submit_data.n_f_idx_i = 0;
						case = 1;
						meta.c_temp = meta.read_counter_2;
					}
				}
				step1 = 1;
			}

			// compute delta after read bucket fields
			if (step1 == 1){

				if ((meta.bit_res == 0) && (meta.index_i < L)) {
					
					// probabilistic replacement
					meta.rng_1 = random_generator1.get();
					calc_cond();
					if ((case == 1) && (meta.cond < 65536)) {
						ig_dprsr_md.resubmit_type = 2;
						meta.submit_data.case_slot_idx [31:30] = 2w3;
					}				

					meta.submit_data.n_f_idx_i [4:0] = meta.index_i;

					meta.submit_data.case_slot_idx [0:0] = meta.bucket_idx;
			}
		}
		else{

			//compute delta
			cal_ns_delta.apply();
			read_ns();
			
			cal1();
			cal2();
			cal3();
			cal4();

			@in_hash {
				meta.n_f_n_s1 = (bit<20>) (meta.n_f_n_s [31:12]);
			}

			cal_gamma.apply();

			meta.delta = 0;
			if (meta.index_i == meta.gamma) {
				meta.delta = (bit<32>) meta.n_f_n_s + 32w1010100111;
			}

			meta.rng_2 = random_generator2.get();

			compute_index_u();

			if (meta.submit_data.case_slot_idx [31:30] == 1) {

				if (meta.submit_data.case_slot_idx [0:0] == 0) {
					modify_B_key_1_action();
					modify_B_value_1_action();
				}
				else {
					modify_B_key_2_action();
					modify_B_value_2_action();
				}

			}

			if (meta.submit_data.case_slot_idx [31:30] == 3) {
				
			}

		}

		


			// resubmitted packet processing
			

		}
	}
}

control IngressDeparser(packet_out pkt,
	inout ingress_headers_t hdr,
	in ingress_metadata_t meta,
	in ingress_intrinsic_metadata_for_deparser_t ig_dprtr_md)

{
    Resubmit() resubmit;

    apply{
        if(ig_dprtr_md.resubmit_type == 2) {
            resubmit.emit(meta.submit_data);
        }
		pkt.emit(hdr);
    }
}

/* egress */
parser EgressParser(packet_in pkt,
	out egress_headers_t hdr,
	out egress_metadata_t meta,
	out egress_intrinsic_metadata_t eg_intr_md)
{
	state start{
		pkt.extract(eg_intr_md);
		transition accept;
	}
}

control Egress(inout egress_headers_t hdr,
	inout egress_metadata_t meta,
	in egress_intrinsic_metadata_t eg_intr_md,
	in egress_intrinsic_metadata_from_parser_t eg_prsr_md,
	inout egress_intrinsic_metadata_for_deparser_t eg_dprsr_md,
	inout egress_intrinsic_metadata_for_output_port_t eg_oport_md)
{
	apply{}
}

control EgressDeparser(packet_out pkt,
	inout egress_headers_t hdr,
	in egress_metadata_t meta,
	in egress_intrinsic_metadata_for_deparser_t eg_dprsr_md)
{
	apply{
		pkt.emit(hdr);
	}
}


/* main */
Pipeline(IngressParser(),Ingress(),IngressDeparser(),
EgressParser(),Egress(),EgressDeparser()) pipe;

Switch(pipe) main;
