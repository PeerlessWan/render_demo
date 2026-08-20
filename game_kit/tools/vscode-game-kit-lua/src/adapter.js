"use strict";
const net = require("net");

const host = process.env.GAME_KIT_DAP_HOST || "127.0.0.1";
const port = parseInt(process.env.GAME_KIT_DAP_PORT || "4711", 10);

const sock = net.connect({ host, port }, () => {
  process.stdin.pipe(sock);
  sock.pipe(process.stdout);
});
sock.on("error", (err) => {
  process.stderr.write(String(err) + "\n");
  process.exit(1);
});
