from marionette_harness import MarionetteTestCase

from Xlib import display
from Xlib.protocol import rq

class ResQueryClients(rq.ReplyRequest):
    _request = rq.Struct(
        rq.Card8('opcode'),
        rq.Opcode(1),
        rq.RequestLength(),
    )
    _reply = rq.Struct(
        rq.ReplyCode(),
        rq.Pad(1),
        rq.Card16('sequence_number'),
        rq.ReplyLength(),
        rq.Card32('num_clients'),
        rq.Pad(20),
    )

# FIXME should cache the display
def num_clients():
    display = display.Display()
    ext = display.query_extension("X-Resource")
    opcode = ext.major_opcode
    cli = ResQueryClients(display = display.display, opcode = opcode)
    return cli.num_clients

class AvoidX11TestCase(MarionetteTestCase):
    def test_avoid_x11(self):
        clients_before = num_clients()
        self.marionette.open("http://example.com")
        clients_during = num_clients()
        self.marionette.close()
        clients_after = num_clients()

        self.assertEqual(clients_before, clients_during)
        self.assertEqual(clients_before, clients_after)
