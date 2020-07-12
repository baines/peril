
static inline uint32_t murmur2(const void* key, int len) {
	const uint32_t m = 0x5bd1e995;
	const int r = 24;

	uint32_t h = 0x1234 ^ len;
	const uint8_t* data = (const uint8_t*)key;

	while(len >= 4){
		uint32_t k = *(uint32_t*)data;

		k *= m;
		k ^= k >> r;
		k *= m;

		h *= m;
		h ^= k;

		data += 4;
		len -= 4;
	}

	switch(len){
		case 3: h ^= data[2] << 16; /* fall-thru */
		case 2: h ^= data[1] << 8; /* fall-thru */
		case 1: h ^= data[0];
		        h *= m;
	};

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}
