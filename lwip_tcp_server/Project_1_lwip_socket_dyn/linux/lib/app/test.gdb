b tcp_in.c:368
commands 1
  print pcb->rcv_nxt
  print in_data
  continue
end
b sockets.c:577
commands 2
  print sock->conn->pcb.tcp->rcv_nxt
  p len
  p done
  continue
end
run 10.0.0.1
