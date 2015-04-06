import neovim
import os.path

from cffi import FFI
ffi = FFI()
ffi.cdef("""

typedef struct highlight_t {
    size_t line;
    size_t col;
    size_t length;
    const char* group;
} highlight_t;

typedef void (*color_coded_callback_T)(int buf, highlight_t hl[], size_t n_hl);

// request that buffer buf should be highligted according to (file, data)
// callback will be invoked on separate thread
// *might* supercede earlier request on buf
int color_coded_request(color_coded_callback_T callback, int buf, char* file, char* data);
""")
fn =  os.path.join( os.path.dirname(__file__), '../../bin/color_coded_nvim.so')
api = ffi.dlopen(fn)

from collections import namedtuple
Item = namedtuple('Item', ['line', 'col', 'end', 'group'])

@neovim.plugin
class CCPlugin(object):
    def __init__(self, vim):
        self.vim = vim
        self.cb = ffi.callback("color_coded_callback_T", self._cb)
        self.reqdata = {}
        self.nreq = 0
        self.cur_hl = {}

    def next_hlid(self, *args):
        return self.vim.session.request('buffer_add_highlight',
                self.vim.current.buffer, 0, "", 1, 1, 1)

    def add_highlight(self, *args):
        self.vim.session.request('buffer_add_highlight', *args, async=True)

    def clear_highlight(self, *args):
        return self.vim.session.request('buffer_clear_highlight', *args)


    #this is on a thread
    def _cb(self, buf, hl, n_hl):
        global q
        items = []
        for i in range(n_hl):
            items.append(Item(hl[i].line, hl[i].col, hl[i].col+hl[i].length-1, ffi.string(hl[i].group)))
        self.vim.session.threadsafe_call(self.on_highlight, buf, items)

    def on_highlight(self, data, items):
        buf = self.reqdata.pop(data)
        if not items:
            return
        #buf = neovim.api.Buffer(self.vim.session, buf)
        hlid = self.next_hlid()
        for i in items:
            self.add_highlight(buf, hlid, i.group, i.line, i.col, i.end)
        old = self.cur_hl.get(buf)
        if old is not None:
            self.clear_highlight(buf, old, 1, -1)
        self.cur_hl[buf] = hlid

    @neovim.function("CCHighlight", sync=True)
    def request_highlight(self, args):
        buf = self.vim.current.buffer
        name = buf.name
        data = '\n'.join(buf[:])
        self.reqdata[self.nreq] = buf
        api.color_coded_request(self.cb, self.nreq, name, data)
        self.nreq = self.nreq+1

