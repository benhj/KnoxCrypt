/*
  Copyright (c) <2014>, <BenHJ>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
  3. Neither the name of the copyright holder nor the names of its contributors
  may be used to endorse or promote products derived from this software without
  specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/



#ifndef LOADERTHREAD_H
#define LOADERTHREAD_H

#include "teasafe/CoreTeaSafeIO.hpp"
#include "teasafe/TeaSafe.hpp"
#include "utility/EventType.hpp"

#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>

#include <QThread>
#include <memory>

namespace teasafe
{
    class TeaSafe;
}

typedef std::shared_ptr<teasafe::TeaSafe> SharedTeaSafe;

class LoaderThread : public QThread
{
    Q_OBJECT
        public:
    explicit LoaderThread(QObject *parent = 0);

    void setSharedIO(teasafe::SharedCoreIO const &io);

    SharedTeaSafe getTeaSafe();

  protected:
    void run();

  signals:

    void finishedLoadingSignal();

  private:
    teasafe::SharedCoreIO m_io;
    SharedTeaSafe m_teaSafe;
    typedef boost::mutex TeaMutex;
    typedef boost::lock_guard<TeaMutex> TeaLock;
    mutable TeaMutex m_teaMutex;

    void loadTSImage();

};

#endif // LOADERTHREAD_H
