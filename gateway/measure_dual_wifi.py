"""Measure bounded HTTP downloads through explicitly selected Windows adapters."""
import argparse, concurrent.futures, http.client, json, socket, struct, time
from datetime import datetime
from pathlib import Path

IP_UNICAST_IF = 31  # Winsock ws2ipdef.h; DWORD interface index in network byte order.
ENDPOINTS = {'ramona': ('10.116.0.4',25), 'grillo': ('10.148.0.2',6)}

def connect(node,timeout=3):
    source,index=ENDPOINTS[node]
    s=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    try:
        s.setsockopt(socket.IPPROTO_IP,IP_UNICAST_IF,struct.pack('!I',index))
        s.bind((source,0)); s.settimeout(timeout)
        s.connect(('10.255.255.254',8765))
        return s
    except BaseException:
        s.close(); raise

def measure(node,seconds,path,samples=None,timeout=3):
    start=time.monotonic(); deadline=start+seconds; size=0; errors=[]; completed=0
    next_sample=start+5
    def sample(event):
        if samples is None: return
        row={'time':datetime.now().astimezone().isoformat(),'node':node,
             'elapsed_s':time.monotonic()-start,'bytes':size,'errors':len(errors),
             'completed_downloads':completed,'event':event}
        with samples.open('a',encoding='utf-8') as f:
            f.write(json.dumps(row)+'\n')
    sample('start')
    while time.monotonic()<deadline:
        s=None
        stage='connect'
        try:
            s=connect(node,timeout)
            stage='send_request'
            s.sendall(f'GET {path} HTTP/1.1\r\nHost: 10.255.255.254:8765\r\nConnection: close\r\n\r\n'.encode())
            stage='response_headers'
            response=http.client.HTTPResponse(s); response.begin()
            if response.status!=200: raise RuntimeError(f'HTTP {response.status}')
            stage='response_body'
            while time.monotonic()<deadline:
                s.settimeout(max(.1,min(timeout,deadline-time.monotonic())))
                block=response.read1(65536)
                if not block: completed+=1; break
                size+=len(block)
                if time.monotonic()>=next_sample:
                    sample('progress');next_sample=time.monotonic()+5
            response.close()
            if path=='/': break
        except Exception as e:
            if time.monotonic() >= deadline:
                break  # End of the scheduled interval, not a network failure.
            errors.append({'time':datetime.now().astimezone().isoformat(),'error':str(e),'stage':stage})
            sample('error'); time.sleep(.2)
        finally:
            if s: s.close()
    sample('end')
    elapsed=time.monotonic()-start
    return {'node':node,'source':ENDPOINTS[node][0],'ifindex':ENDPOINTS[node][1],
            'bytes':size,'elapsed_s':elapsed,'mbps':size*8/elapsed/1e6,
            'completed_downloads':completed,'errors':errors}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--smoke',action='store_true')
    p.add_argument('--ramona-ip');p.add_argument('--grillo-ip');p.add_argument('--timeout',type=float,default=3);p.add_argument('--connections',type=int,default=1);p.add_argument('--phase',choices=('all','both','grillo_then_both'),default='all');p.add_argument('--seconds',type=int,default=20);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(); results=[]
    for name in ('ramona','grillo'):
        source=getattr(args,name+'_ip')
        if source:
            import ipaddress
            addr=ipaddress.IPv4Address(source)
            if addr.is_link_local or addr.is_unspecified or addr.is_multicast:
                p.error('A valid client IPv4 address is required')
            ENDPOINTS[name]=(str(addr),ENDPOINTS[name][1])
    if not 1 <= args.seconds <= 600: p.error('seconds must be between 1 and 600')
    if not 1 <= args.connections <= 8: p.error('connections must be between 1 and 8')
    if not 0 < args.timeout <= 60: p.error('timeout must be greater than 0 and at most 60')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    phases=[('preflight',[n]) for n in ENDPOINTS] if args.smoke else [('ramona_only',['ramona']),('grillo_only',['grillo']),('both',['ramona','grillo'])]
    if not args.smoke and args.phase=='both': phases=[('both',['ramona','grillo'])]
    if not args.smoke and args.phase=='grillo_then_both': phases=[('grillo_only',['grillo']),('both',['ramona','grillo'])]
    for phase,nodes in phases:
        print('START',phase,datetime.now().astimezone().isoformat(),flush=True)
        jobs=[(n,i) for n in nodes for i in range(1 if args.smoke else args.connections)]
        def run(job):
            n,i=job
            row=measure(n,8 if args.smoke else args.seconds,'/' if args.smoke else '/carga.bin',
                args.output.with_name(args.output.stem+'-'+phase+'-'+n+'-'+str(i)+'-samples.jsonl'),timeout=args.timeout)
            row['worker']=i
            return row
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(jobs)) as pool:
            rows=list(pool.map(run,jobs))
        summary=[]
        for n in nodes:
            selected=[r for r in rows if r['node']==n]
            elapsed=max(r['elapsed_s'] for r in selected)
            summary.append({'node':n,'connections':len(selected),'bytes':sum(r['bytes'] for r in selected),
                'mbps':sum(r['bytes'] for r in selected)*8/elapsed/1e6,
                'errors':sum(len(r['errors']) for r in selected),
                'completed_downloads':sum(r['completed_downloads'] for r in selected)})
        results.append({'phase':phase,'results':rows,'summary':summary,'timeout_s':args.timeout})
        args.output.write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
        print(json.dumps({'phase':phase,'summary':summary,'timeout_s':args.timeout}),flush=True)
        if args.smoke and any(not r['bytes'] for r in rows): raise SystemExit('Preflight failed; do not interpret performance')
        time.sleep(2)