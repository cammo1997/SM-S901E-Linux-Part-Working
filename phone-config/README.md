Phone-side config we ended up with (copy to the same paths under `/` on the phone, `chmod +x` the scripts, then
`rc-update add s22-clock boot`). `s22-bringup.conf` is a reconstruction, `s22-clock*` and `s22-usb-internet.start` are the real files.
Not included (no redistribution): Samsung firmware - GPU zap shader, a730_sqe.fw, gmu_gen70000.bin, Wi-Fi/BT firmware. Extract them from
your own phone; paths are in docs/boot-log.md ("Attempt 12").
