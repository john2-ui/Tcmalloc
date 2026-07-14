#include "../include/thread_cache.hpp"

thread_cache **thread_cache_tls_address_from_b() { return &p_tls_thread_cache; }
