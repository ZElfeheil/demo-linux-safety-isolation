FROM alpine AS base
RUN mkdir -p /out && echo "test image" > /out/Image && echo "test initramfs" > /out/initramfs.cpio.gz

FROM scratch AS artifacts
COPY --from=base /out/Image /out/Image
COPY --from=base /out/initramfs.cpio.gz /out/initramfs.cpio.gz
