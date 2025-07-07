.ONESHELL:
SHELL = /bin/bash
.SHELLFLAGS += -e

MAIN_TARGET = sonic-ue-linkd_1.0.0_amd64.deb

$(addprefix $(DEST)/, $(MAIN_TARGET)): $(DEST)/% :
	# Build the daemon
	pushd src
	
	# Compile the daemon
	g++ -std=c++14 -O2 -g \
		-I/usr/include/swss \
		-I/usr/include/sai \
		-lswsscommon -lsairedis \
		-o ue-linkd \
		ue_linkd.cpp \
		ue_llr_manager.cpp \
		ue_pri_manager.cpp
	
	popd
	
	# Create debian package
	dpkg-buildpackage -rfakeroot -b -us -uc
	mv ../sonic-ue-linkd_*.deb $(DEST)/$(MAIN_TARGET)
