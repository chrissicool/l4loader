/*
 * License:
 * This file is largely based on code from the L4Linux project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. This program is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 */

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
