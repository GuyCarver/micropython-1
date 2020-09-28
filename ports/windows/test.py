from guy import test
from utime import sleep_ms

#class mytest(object):
#  '''docstring for mytest'''
#  def __init__( self ):
#    super(mytest, self).__init__()
#    self._val = 0
#    self._callback = None
#
#  def update( self ):
#    '''  '''
#    self._val += 1
#    if (self._val % 10) == 0:
#      if self._callback != None:
#        self._callback(self._val)


def dotest(  ):
  ''' '''
  t = test()

  sound = 'a.mp3'

  t.play(sound)
  sleep_ms(1000)
#  t.pause(sound)
#  sleep_ms(2000)
#  t.resume(sound)
#  sleep_ms(200)
  t.stop(sound)

