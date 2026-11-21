import argparse,json,struct,time
from pathlib import Path
from serial.tools import list_ports
from watch_client import Watch
parser=argparse.ArgumentParser(description='Run three live monitor checks with a temporary synthetic baseline, then restore the original baseline and history.')
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--port')
parser.add_argument('--temporary-baseline',action='store_true',help='Explicitly allow the backed-up test baseline; start from home with Room Watch highlighted.')
args=parser.parse_args()
if not args.temporary_baseline:parser.error('This hardware test requires --temporary-baseline and a closed Room Watch app')
ports=[p.device for p in list_ports.comports() if (p.vid,p.pid,p.serial_number)==(0x483,0x5750,'3177384F3434')]
if not args.port and len(ports)!=1:parser.error('Connect one M1 or specify --port')
out=args.output;out.mkdir(parents=True,exist_ok=True);w=Watch(args.port or ports[0])
report={'passed':False,'test_type':'Live radio captures with temporary synthetic baseline; not a real intrusion or occupancy event','states':[]}
original=None;old_events=None;events_existed=False;fixture_written=False;backup_path=None
try:
 assert not w.status()['active']
 original=w.read_small('System/room-watch.dat');assert len(original)==70
 try:old_events=w.read_small('System/room-watch-events.csv');events_existed=True
 except RuntimeError as e:
  if 'error 05' not in str(e):raise
 backup_path='System/watch-test-'+str(int(time.time()))+'.dat'
 w.write_small(backup_path,original);report['sd_recovery_copy']=backup_path
 fixture=bytearray(original);struct.pack_into('<HHh',fixture,55,4096,253,-127)
 h=2166136261
 for byte in fixture[:66]:h=((h^byte)*16777619)&0xffffffff
 struct.pack_into('<I',fixture,66,h)
 fixture_written=True
 w.write_small('System/room-watch.dat',bytes(fixture))
 w.click(0);w.wait(lambda s:s['active'] and s['phase']==0,'Open monitor')
 assert w.status()['interval']==0 and w.status()['alerts']==1
 for page in (1,2):w.click(3);w.wait(lambda s:s['page']==page,'Monitor page')
 w.click(0);w.wait(lambda s:s['running'],'Start monitoring');start=time.monotonic();last=0
 while time.monotonic()-start<300:
  state=w.status()
  if not state['active'] or not state['running']:raise RuntimeError('Monitor stopped: '+json.dumps(state))
  if state['checks']>last and state['phase']==0:
   last=state['checks'];report['states'].append(state)
   print('Completed scheduled scan',last,'alerts',state['alert_count'],flush=True)
   if last==1:assert state['alert_count']==0,'Alert fired without confirmation'
   if last>=3:break
  time.sleep(.5)
 assert len(report['states'])>=3,'Three automatic checks did not finish'
 a,b,c=report['states'][:3]
 assert b['alert_count']==1 and c['alert_count']==1,'Expected one confirmed alert with repeats suppressed'
 gaps=[(b['finished_ms']-a['finished_ms'])&0xffffffff,(c['finished_ms']-b['finished_ms'])&0xffffffff]
 assert all(gap>=60000 for gap in gaps);report['completion_spacing_ms']=gaps
 (out/'confirmed-alert-screen.bin').write_bytes(w.request(0x13,expected=0x12,timeout=5))
 w.click(5);stopped=w.wait(lambda s:not s['running'] and s['active'],'Back stops monitoring');report['stop_verified']=stopped
 w.close_app();events=w.read_small('System/room-watch-events.csv');(out/'synthetic-alert-events.csv').write_bytes(events)
 fresh=events[len(old_events or b''):].decode().splitlines();assert len(fresh)==1,fresh
 bits=int(fresh[0].split(',')[1]);assert bits&26==26,bits;report['confirmed_event_bits']=bits
 report['passed']=True
except Exception as e:
 report['error']=str(e)
 try:report['last_status']=w.status()
 except Exception:pass
finally:
 try:
  w.close_app()
  if fixture_written:
   w.write_small('System/room-watch.dat',original);report['baseline_restored_and_verified']=True
   if events_existed:w.write_small('System/room-watch-events.csv',old_events)
   else:
    try:w.request(0x37,b'System/room-watch-events.csv\0',6,10)
    except RuntimeError as e:
     if 'error 05' not in str(e):raise
   report['synthetic_history_removed']=True
  if backup_path and report.get('baseline_restored_and_verified'):
   assert w.read_small(backup_path)==original;w.request(0x37,backup_path.encode()+b'\0',6,10);report['temporary_sd_copy_removed']=True
 except Exception as e:report['cleanup_error']=str(e)
 w.port.close();(out/'automatic-confirmation-check.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2),flush=True)

raise SystemExit(0 if report.get("passed") and not report.get("cleanup_error") else 1)
