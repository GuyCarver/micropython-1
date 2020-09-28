from guy import PS2
from utime import sleep_ms

#def cb( ind, status ) :
#  inname = p.inputname(ind)
#  if status & 2 :
#    statname = 'pressed' if status == 3 else 'released'
#    print(inname + ': ' + statname)
#
#p.callback(cb)
#

def test(  ):
  ''' '''
  p = PS2(23, 19, 18, 5)

  def cb( ind, status ) :
    if status & 2 :
      inname = p.inputname(ind)
      statname = 'pressed' if status == 3 else 'released'
      print(inname + ': ' + statname)

  p.callback(cb)

  while 1:
    p.update()
    sleep_ms(100)
