# Complete disk image backups

`image_files` contains complete **128 MiB / 134217728-byte `.img` files** and,
from the auth-mask diagnostic revision onward, their compressed `.zip` copies
directly inside it. Extract each ZIP before selecting the `.img` in a flashing tool. There are no
hash subfolders, raw kernels, test fixtures, catalogs or JSON sidecars in this
folder. The earlier hash-folder layout has been replaced.

Examples of restored backups:

```text
image_files/
  OurOS-Pi5-header-boot-verified.img
  OurOS-Pi5-header-memory-verified.img
  OurOS-Pi5-header-ramfs-verified.img
  OurOS-Pi5-header-usb-fat-observed.img
  OurOS-Pi5-header-wifi-upload-verified.img
  OurOS-Pi5-header-wifi-startup-verified.img
  OurOS-Pi5-header-wifi-control-verified.img
  OurOS-Pi5-header-wifi-prepare-verified.img
  OurOS-Pi5-header-wifi-scan-verified.img
  OurOS-Pi5-header-wifi-auth-foundation-verified.img
  OurOS-Pi5-header-experimental-sd.img
```

The newest header-console image is always
`image_files/OurOS-Pi5-header-experimental-sd.img`. Original phase backup names
are retained. Complete earlier builds without a saved milestone name use
`OurOS-Pi5-header-earlier-build-NN.img`; those numbers do not claim chronological
order or hardware verification. The original dedicated-UART disk image is kept
as `OurOS-Pi5-experimental-sd.img`.

Build with the usual command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/image-rpi5.ps1 -Console header
```

Before replacing a completed image, the scripts save it to `image_files`. If its
bytes already match the same-named backup, no duplicate is created. On a changed
build the previous `experimental-sd.img` gets a date/time suffix, for example
`OurOS-Pi5-header-experimental-sd-20261109-173000-123.img`, and the newest image
takes the usual filename. Existing named milestone files are also preserved
if a different image arrives with the same name. SHA256 is checked during copying.

Raw `ouros-rpi5.img` kernels remain in `build` and never enter `image_files`.
After a successful image build, known temporary staging/extraction folders,
old redundant backups and test-image copies are cleaned from `build`. Current
images, ELF symbols, maps, boot ZIPs and useful test logs remain available.
Cleanup is restricted to the project's build tree and refuses junctions/symlinks.
Run builds and cleanup sequentially.

Manual maintenance and validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/archive-images.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/clean-build.ps1 -IncludeTestImages -IncludePreviewCaches
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-image-archive.ps1
```

Raw images remain local and ignored by Git. Only `.zip` files in this folder
are tracked and published with the corresponding project update. Existing raw
phase backups are retained; ZIP publication starts with the current revision.
`image-rpi5.ps1` automatically runs `compress-image.ps1` after image validation.
The ZIP contains exactly one complete disk, with its original `.img` name.
Compression streams the image, then independently decompresses the entry to
SHA256 and compares it with the source before promotion. An unchanged image
reuses its ZIP; replacing a ZIP first preserves the old ZIP under a dated name.
Archives at or above 100,000,000 bytes are rejected before promotion. A build
generates artifacts; committing and pushing them is a separate Git operation.
Current user preference: keep development changes and image ZIPs local while
working toward connection to Jack's. The user handles GitHub publication;
do not commit or push each phase automatically.

To compress an existing complete image without rebuilding:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/compress-image.ps1 -Image image_files/OurOS-Pi5-header-experimental-sd.img
```

Back up local raw history separately if needed. Image
copy verification does not imply hardware boot success; consult the phase's
proof log. No physical device is written by these scripts.
