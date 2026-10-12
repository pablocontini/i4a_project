"""Inspect classic Ethernet PCAP; exact repeated TCP payload segments are candidates, not Wireshark labels."""
import argparse, collections, ipaddress, json, struct, zlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('pcap',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
flows={}; packets=0; first=None;last=None
with a.pcap.open('rb') as f:
 h=f.read(24);magic=h[:4]
 endian={b'\xd4\xc3\xb2\xa1':'<',b'\xa1\xb2\xc3\xd4':'>',b'\x4d\x3c\xb2\xa1':'<',b'\xa1\xb2\x3c\x4d':'>'}.get(magic)
 if endian is None: raise SystemExit('Unsupported PCAP format')
 ns=magic in (b'\x4d\x3c\xb2\xa1',b'\xa1\xb2\x3c\x4d')
 link=struct.unpack(endian+'I',h[20:24])[0]
 if link!=1: raise SystemExit(f'Unsupported link type {link}')
 while hdr:=f.read(16):
  if len(hdr)!=16: raise SystemExit('Truncated packet header')
  sec,frac,cap,wire=struct.unpack(endian+'IIII',hdr);data=f.read(cap)
  if len(data)!=cap: raise SystemExit('Truncated packet')
  ts=sec+frac/(1e9 if ns else 1e6);packets+=1;first=ts if first is None else first;last=ts
  if len(data)<54 or data[12:14]!=b'\x08\x00': continue
  ip=data[14:];ihl=(ip[0]&15)*4
  if ip[9]!=6 or struct.unpack('!H',ip[6:8])[0]&0x3fff: continue
  total=struct.unpack('!H',ip[2:4])[0];ip=ip[:total]
  src=str(ipaddress.IPv4Address(ip[12:16]));dst=str(ipaddress.IPv4Address(ip[16:20]));tcp=ip[ihl:]
  if len(tcp)<20: continue
  sp,dp,seq,ack=struct.unpack('!HHII',tcp[:12]);th=(tcp[12]>>4)*4;flags=tcp[13];win=struct.unpack('!H',tcp[14:16])[0];payload=tcp[th:]
  if src=='10.255.255.254' and sp==8765: client=dst;cp=dp;direction='server'
  elif dst=='10.255.255.254' and dp==8765: client=src;cp=sp;direction='client'
  else: continue
  key=(client,cp);flow=flows.setdefault(key,{'client':client,'port':cp,'start':ts,'end':ts,'server':[],'client_ack':[],'rst':[],'syn':[]})
  flow['end']=ts
  if flags&4:flow['rst'].append({'time':ts,'from':direction})
  if flags&2:flow['syn'].append({'time':ts,'from':direction})
  if direction=='server' and payload: flow['server'].append((ts,seq,len(payload),zlib.crc32(payload)))
  if direction=='client' and flags&16 and not flags&4:flow['client_ack'].append((ts,ack,win,len(payload),flags))
reports=[]
for flow in flows.values():
 data=flow['server'];seen={};dups=[];new=[];buckets=collections.Counter()
 for ts,seq,n,crc in data:
  key=(seq,n,crc)
  if key in seen:dups.append({'time':ts,'seq':seq,'bytes':n,'delay_s':ts-seen[key]})
  else:seen[key]=ts;new.append((ts,n))
  buckets[int((ts-flow['start'])//5)]+=n
 acks=flow['client_ack']; dupack=0;zero=0;previous=None
 for ts,ack,win,n,flags in acks:
  if win==0:zero+=1
  if n==0 and not flags&7:
   if previous==(ack,win):dupack+=1
   previous=(ack,win)
 gaps=sorted([{'start':x[0],'end':y[0],'seconds':y[0]-x[0]} for x,y in zip(new,new[1:])],key=lambda x:x['seconds'],reverse=True)
 reports.append({'client':flow['client'],'port':flow['port'],'start_epoch':flow['start'],'end_epoch':flow['end'],
 'payload_packets':len(data),'payload_bytes':sum(x[2] for x in data),'first_seen_payload_bytes':sum(x[1] for x in new),
 'exact_repeat_segments':len(dups),'exact_repeat_bytes':sum(x['bytes'] for x in dups),'repeat_examples':dups[:8],
 'client_ack_packets':len(acks),'consecutive_same_ack_and_window':dupack,'client_zero_window_packets':zero,
 'client_min_raw_window':min((x[2] for x in acks),default=None),'rst':flow['rst'],'syn_count':len(flow['syn']),
 'largest_new_payload_gaps':gaps[:3],'five_second_payload_bytes':dict(sorted(buckets.items()))})
out={'packets':packets,'capture_start_epoch':first,'capture_end_epoch':last,'flows':reports,
 'limitations':'Exact same seq/length/content repeats are retransmission candidates; capture duplication cannot be excluded. Resegmented overlaps are not counted. Same ACK counts are not Wireshark TCP-analysis classifications. Window raw values are unscaled. RST may be caused by intentional test end.'}
a.output.write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8')
for client in sorted({r['client'] for r in reports}):
 bulk=[r for r in reports if r['client']==client and r['payload_bytes']>100000]
 print(json.dumps({'client':client,'bulk_flows':len(bulk),'payload_packets':sum(r['payload_packets'] for r in bulk),
 'exact_repeat_segments':sum(r['exact_repeat_segments'] for r in bulk),'exact_repeat_bytes':sum(r['exact_repeat_bytes'] for r in bulk),
 'zero_window_packets':sum(r['client_zero_window_packets'] for r in bulk),'same_ack_window':sum(r['consecutive_same_ack_and_window'] for r in bulk),
 'max_new_data_gap_s':max((r['largest_new_payload_gaps'][0]['seconds'] for r in bulk if r['largest_new_payload_gaps']),default=0)}))