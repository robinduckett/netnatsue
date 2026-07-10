# -*- Makefile -*- module for Engine
# Replaces c2e/modules/netbabel/module.mk.
#
# The proprietary Babel libraries (BabelCloak/BabelClient/
# BabelCommon) were lost; the NetNatsue library in ../Babel/NetNatsue
# recreates the client side of the NetBabel protocol and provides
# the same headers, so the module wrapper builds unchanged.

NETBABEL_SOURCES := modules/netbabel/NetHandlers.cpp \
					modules/netbabel/NetLogImplementation.cpp \
					modules/netbabel/NetworkImplementation.cpp \
					../Babel/NetNatsue/DSNetManager.cpp \
					../Babel/NetNatsue/NetMemoryPack.cpp \
					../Babel/NetNatsue/NetMemoryUnpack.cpp \
					../Babel/NetNatsue/NetNatsueProtocol.cpp \
					../Babel/NetNatsue/NetNatsueSocket.cpp \
					server/HistoryFeed/HistoryTransferOut.cpp

lc2e-netbabel.so: $(patsubst %.cpp, %.o, $(NETBABEL_SOURCES))
lc2e-netbabel.do: $(patsubst %.cpp, %.od, $(NETBABEL_SOURCES))
lc2e-netbabel.ho: $(patsubst %.cpp, %.oh, $(NETBABEL_SOURCES))
