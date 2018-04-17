# Controller for the quicrun 1060 Electronic Speed Control (ESP)
#This controller works through the pca9865 servo controller.

from time import sleep_ms

#todo: May need to move speed values over time if the battery cannot handle it.

class quicrun(object):
  '''docstring for quicrun'''
  STOP = const(50)
  FORWARD_MAX = const(68)
  FORWARD_MIN = const(52)
  BACKWARD_MAX = const(30)
  BACKWARD_MIN = const(48)
  BACKWARD_INIT = const(45)

  @staticmethod
  def getperc( aMin, aMax, aPerc  ) :
    return (((aMax - aMin) * aPerc) // 100) + aMin

  def __init__(self, aPCA, aIndex):
    super(quicrun, self).__init__()
    self._pca = aPCA
    self._index = aIndex
    self.reset()

  def reset( self ) :
    self._pca.set(self._index, 75)
    sleep_ms(500)
    self._pca.set(self._index, 100)
    sleep_ms(500)
    self._pca.set(self._index, STOP)
    self._curspeed = 0

  def _set( self, aValue ) :
    self._pca.set(self._index, aValue)

  def _reverse( self ) :
    if self._currspeed >= 0 :
      self._set(STOP)
      sleep_ms(100)
      self._set(BACKWARD_INIT)
      sleep_ms(100)
      self._set(STOP)
      sleep_ms(100)

  def speed( self, aSpeed ) :
    '''Set speed -100 to 100.'''
    aSpeed = max(min(100, aSpeed), -100)

    if aSpeed == 0 :
      self._set(STOP)
    else:
      if aSpeed > 0 :
        self._set(quicrun.getperc(FORWARD_MIN, FORWARD_MAX, aSpeed))
      else:
        self._reverse()
        self._set(quicrun.getperc(BACKWARD_MAX, BACKWARD_MIN, 100 + aSpeed))

    self._currspeed = aSpeed

