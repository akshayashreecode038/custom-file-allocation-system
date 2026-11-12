from __future__ import annotations
import ctypes, json, shutil, subprocess, tempfile, threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse
ROOT=Path(__file__).resolve().parent
DLL=Path(tempfile.gettempdir())/"file_allocation_studio_backend.dll"
LOCK=threading.Lock()
ERRORS={1:"Use 1–19 letters, numbers, dots, dashes, or underscores for the file name.",2:"A file with that name already exists.",3:"File size must be greater than zero.",4:"That file is larger than the simulated disk.",5:"Choose a valid allocation method.",6:"The file directory is full.",7:"There are not enough free blocks for this file.",8:"Enough free space exists, but no continuous run is large enough.",9:"This file needs more pointers than one index block can hold.",10:"The C backend reported a consistency problem."}
def build_backend():
 compiler=shutil.which("gcc")
 if not compiler: raise RuntimeError("GCC is required. Install MinGW GCC and make sure gcc is on PATH.")
 newest=max((ROOT/"main.c").stat().st_mtime,(ROOT/"fas_web.c").stat().st_mtime)
 if DLL.exists() and DLL.stat().st_mtime>=newest:return
 r=subprocess.run([compiler,"-std=c11","-O2","-shared","-o",str(DLL),str(ROOT/"fas_web.c")],cwd=ROOT,capture_output=True,text=True)
 if r.returncode: raise RuntimeError("Could not build the C backend:\n"+r.stderr.strip())
def load_backend():
 build_backend();lib=ctypes.CDLL(str(DLL))
 lib.web_init.argtypes=[ctypes.c_int];lib.web_init.restype=ctypes.c_int
 lib.web_create.argtypes=[ctypes.c_char_p,ctypes.c_int,ctypes.c_int];lib.web_create.restype=ctypes.c_int
 lib.web_delete.argtypes=[ctypes.c_char_p];lib.web_delete.restype=ctypes.c_int
 lib.web_state.argtypes=[ctypes.c_char_p,ctypes.c_size_t];lib.web_state.restype=ctypes.c_int
 lib.web_init(32);return lib
backend=load_backend()
def state():
 b=ctypes.create_string_buffer(65536);n=backend.web_state(b,len(b))
 if n<=0:raise RuntimeError("Could not read C backend state.")
 return json.loads(b.value[:n].decode("utf-8"))
class Handler(SimpleHTTPRequestHandler):
 def __init__(self,*a,**kw):super().__init__(*a,directory=str(ROOT),**kw)
 def send_json(self,status,data):
  body=json.dumps(data).encode();self.send_response(status);self.send_header("Content-Type","application/json; charset=utf-8");self.send_header("Cache-Control","no-store");self.send_header("Content-Length",str(len(body)));self.end_headers();self.wfile.write(body)
 def body_json(self):
  try:
   n=int(self.headers.get("Content-Length","0"))
   if not 1<=n<=4096:raise ValueError()
   return json.loads(self.rfile.read(n))
  except (ValueError,json.JSONDecodeError):self.send_json(400,{"error":"Please send a valid request."});return None
 def do_GET(self):
  if urlparse(self.path).path=="/api/state":
   with LOCK:self.send_json(200,state())
  else:super().do_GET()
 def do_POST(self):
  path=urlparse(self.path).path;p=self.body_json()
  if p is None:return
  with LOCK:
   if path=="/api/disk":
    try:n=int(p.get("blocks",32))
    except (TypeError,ValueError):n=0
    if not backend.web_init(n):self.send_json(400,{"error":"Disk size must be from 8 to 256 blocks."});return
   elif path=="/api/files":
    try:
     name=str(p.get("name","")).strip();size=int(p.get("size",0));method=int(p.get("method",1));result=backend.web_create(name.encode("utf-8"),size,method)
    except (TypeError,ValueError,UnicodeEncodeError):self.send_json(400,{"error":"Enter a valid file name, size, and method."});return
    if result:self.send_json(400,{"error":ERRORS.get(result,"The file could not be created.")});return
   elif path.startswith("/api/files/"):
    if backend.web_delete(path.removeprefix("/api/files/").encode("utf-8"))<0:self.send_json(404,{"error":"That file could not be found."});return
   else:self.send_json(404,{"error":"Action not found."});return
   self.send_json(200,state())
 def log_message(self,fmt,*args):print("[web] "+fmt%args)
if __name__=="__main__":
 host,port="127.0.0.1",8000;print("File Allocation Studio: http://%s:%d"%(host,port));print("Press Ctrl+C to stop.")
 try:ThreadingHTTPServer((host,port),Handler).serve_forever()
 except KeyboardInterrupt:print("\nServer stopped.")

