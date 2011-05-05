// Include image with some alignment

.section ".rodata"
.p2align 12
.global image_bsd_start
image_bsd_start:
.incbin BSD_IMAGE
.global image_bsd_end
image_bsd_end:
.p2align 12
.previous
