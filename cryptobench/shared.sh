build() {
    make -C ctaes || return 1
    make -C bearssl CONF=PTeX || return 1
    (cd djbsort && ./build && ./test) || return 1
}

ctaes_exe=ctaes/bench
ctaes_args=()

bearssl_exe=bearssl/build/testspeed
bearssl_args=(chacha20_ct)

djbsort_exe=djbsort/link-install/command/int32-speed
djbsort_args=()

benchmarks=(ctaes bearssl djbsort)



