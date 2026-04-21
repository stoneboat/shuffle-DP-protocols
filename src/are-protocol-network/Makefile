MCL_DIR = ../utils/mcl
EMP_DIR = ../utils/emp-tool/install
SSL_DIR = /opt/homebrew/opt/openssl
ARE_DIR = ../are-protocol

CXX      = g++
CXXFLAGS = -std=c++17 -O2 \
           -I. -I$(ARE_DIR) -I$(ARE_DIR)/ot \
           -I$(MCL_DIR)/include \
           -I$(EMP_DIR)/include \
           -I$(SSL_DIR)/include \
           -maes -mpclmul

EMP_RPATH = -Wl,-rpath,$(abspath $(EMP_DIR)/lib)
LDFLAGS   = $(MCL_DIR)/lib/libmcl.a \
            -L$(EMP_DIR)/lib -lemp-tool $(EMP_RPATH) \
            -L$(SSL_DIR)/lib -lcrypto

HEADERS = net_serialize.h \
          $(ARE_DIR)/gc_protocol.h \
          $(ARE_DIR)/metrics.h \
          $(ARE_DIR)/ot/rabin_ot_are.h \
          $(ARE_DIR)/ot/string_ot_are.h \
          $(ARE_DIR)/ot/permxor_are.h \
          $(ARE_DIR)/circuits/build_and_chain.h \
          $(ARE_DIR)/circuits/build_bitonic_sort.h \
          $(ARE_DIR)/circuits/build_dp_selection.h \
          $(ARE_DIR)/circuits/build_sequential_sum.h \
          $(ARE_DIR)/circuits/build_tree_sum.h \
          $(ARE_DIR)/circuits/build_tree_mechanism.h \
          $(ARE_DIR)/circuits/build_linear_contextual.h \
          $(ARE_DIR)/circuits/build_distinct_elements.h \
          $(ARE_DIR)/circuits/build_distinct_elements_histogram.h

TARGETS = evaluator client gen_tables

all: $(TARGETS)

evaluator: evaluator.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

client: client.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

gen_tables: gen_tables.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: clean all
