//This file is part of eMule AI
//Copyright (C)2026 eMule AI
//
#pragma once

// Enable the ngtcp2/GnuTLS QUIC NAT-T transport code path. Runtime availability
// is still checked at startup before the capability is advertised.
#ifndef EMULEAI_ENABLE_QUIC_NATT_RUNTIME
#define EMULEAI_ENABLE_QUIC_NATT_RUNTIME 1
#endif

#define EMULEAI_QUIC_NATT_ALPN "ed2k-ai-natt-quic-v1"
#define EMULEAI_QUIC_NATT_PROOF_MAGIC "EAQN1"
#define EMULEAI_QUIC_NATT_PROOF_LEN (5 + 16 + 16)
