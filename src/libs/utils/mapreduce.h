/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#pragma once

#include "utils_global.h"
#include "runextensions.h"

#include <QFutureWatcher>

namespace Utils {
namespace Internal {

class QTCREATOR_UTILS_EXPORT MapReduceObject : public QObject
{
    Q_OBJECT
};

template <typename ForwardIterator, typename MapResult, typename MapFunction, typename State, typename ReduceResult, typename ReduceFunction>
class MapReduceBase : public MapReduceObject
{
protected:
    static const int MAX_PROGRESS = 1000000;
    // either const or non-const reference wrapper for items from the iterator
    using ItemReferenceWrapper = std::reference_wrapper<typename std::remove_reference<typename ForwardIterator::reference>::type>;

public:
    MapReduceBase(QFutureInterface<ReduceResult> futureInterface, ForwardIterator begin, ForwardIterator end,
              MapFunction &&map, State &state, ReduceFunction &&reduce, int size)
        : m_futureInterface(futureInterface),
          m_iterator(begin),
          m_end(end),
          m_map(std::forward<MapFunction>(map)),
          m_state(state),
          m_reduce(std::forward<ReduceFunction>(reduce)),
          m_handleProgress(size >= 0),
          m_size(size)
    {
        if (m_handleProgress) // progress is handled by us
            m_futureInterface.setProgressRange(0, MAX_PROGRESS);
        connect(&m_selfWatcher, &QFutureWatcher<void>::canceled,
                this, &MapReduceBase::cancelAll);
        m_selfWatcher.setFuture(futureInterface.future());
    }

    void exec()
    {
        if (schedule()) // do not enter event loop for empty containers
            m_loop.exec();
    }

protected:
    virtual void reduce(QFutureWatcher<MapResult> *watcher) = 0;

    bool schedule()
    {
        bool didSchedule = false;
        while (m_iterator != m_end && m_mapWatcher.size() < QThread::idealThreadCount()) {
            didSchedule = true;
            auto watcher = new QFutureWatcher<MapResult>();
            connect(watcher, &QFutureWatcher<MapResult>::finished, this, [this, watcher]() {
                mapFinished(watcher);
            });
            if (m_handleProgress) {
                connect(watcher, &QFutureWatcher<MapResult>::progressValueChanged,
                        this, &MapReduceBase::updateProgress);
                connect(watcher, &QFutureWatcher<MapResult>::progressRangeChanged,
                        this, &MapReduceBase::updateProgress);
            }
            m_mapWatcher.append(watcher);
            watcher->setFuture(runAsync(&m_threadPool, std::cref(m_map),
                                        ItemReferenceWrapper(*m_iterator)));
            ++m_iterator;
        }
        return didSchedule;
    }

    void mapFinished(QFutureWatcher<MapResult> *watcher)
    {
        m_mapWatcher.removeOne(watcher); // remove so we can schedule next one
        bool didSchedule = false;
        if (!m_futureInterface.isCanceled()) {
            // first schedule the next map...
            didSchedule = schedule();
            ++m_successfullyFinishedMapCount;
            updateProgress();
            // ...then reduce
            reduce(watcher);
        }
        delete watcher;
        if (!didSchedule && m_mapWatcher.isEmpty())
            m_loop.quit();
    }

    void updateProgress()
    {
        if (!m_handleProgress) // cannot compute progress
            return;
        if (m_size == 0 || m_successfullyFinishedMapCount == m_size) {
            m_futureInterface.setProgressValue(MAX_PROGRESS);
            return;
        }
        if (!m_futureInterface.isProgressUpdateNeeded())
            return;
        const double progressPerMap = MAX_PROGRESS / double(m_size);
        double progress = m_successfullyFinishedMapCount * progressPerMap;
        foreach (const QFutureWatcher<MapResult> *watcher, m_mapWatcher) {
            if (watcher->progressMinimum() != watcher->progressMaximum()) {
                const double range = watcher->progressMaximum() - watcher->progressMinimum();
                progress += (watcher->progressValue() - watcher->progressMinimum()) / range * progressPerMap;
            }
        }
        m_futureInterface.setProgressValue(int(progress));
    }

    void cancelAll()
    {
        foreach (QFutureWatcher<MapResult> *watcher, m_mapWatcher)
            watcher->cancel();
    }

    QFutureWatcher<void> m_selfWatcher;
    QFutureInterface<ReduceResult> m_futureInterface;
    ForwardIterator m_iterator;
    const ForwardIterator m_end;
    MapFunction m_map;
    State &m_state;
    ReduceFunction m_reduce;
    QEventLoop m_loop;
    QThreadPool m_threadPool; // for reusing threads
    QList<QFutureWatcher<MapResult> *> m_mapWatcher;
    const bool m_handleProgress;
    const int m_size;
    int m_successfullyFinishedMapCount = 0;
};

// non-void result of map function.
template <typename ForwardIterator, typename MapResult, typename MapFunction, typename State, typename ReduceResult, typename ReduceFunction>
class MapReduce : public MapReduceBase<ForwardIterator, MapResult, MapFunction, State, ReduceResult, ReduceFunction>
{
    using BaseType = MapReduceBase<ForwardIterator, MapResult, MapFunction, State, ReduceResult, ReduceFunction>;
public:
    MapReduce(QFutureInterface<ReduceResult> futureInterface, ForwardIterator begin, ForwardIterator end,
              MapFunction &&map, State &state, ReduceFunction &&reduce, int size)
        : BaseType(futureInterface, begin, end, std::forward<MapFunction>(map), state,
                   std::forward<ReduceFunction>(reduce), size)
    {
    }

protected:
    void reduce(QFutureWatcher<MapResult> *watcher) override
    {
        const int resultCount = watcher->future().resultCount();
        for (int i = 0; i < resultCount; ++i) {
            Internal::runAsyncImpl(BaseType::m_futureInterface, BaseType::m_reduce,
                                   BaseType::m_state, watcher->future().resultAt(i));
        }
    }

};

// specialization for void result of map function. Reducing is a no-op.
template <typename ForwardIterator, typename MapFunction, typename State, typename ReduceResult, typename ReduceFunction>
class MapReduce<ForwardIterator, void, MapFunction, State, ReduceResult, ReduceFunction> : public MapReduceBase<ForwardIterator, void, MapFunction, State, ReduceResult, ReduceFunction>
{
    using BaseType = MapReduceBase<ForwardIterator, void, MapFunction, State, ReduceResult, ReduceFunction>;
public:
    MapReduce(QFutureInterface<ReduceResult> futureInterface, ForwardIterator begin, ForwardIterator end,
              MapFunction &&map, State &state, ReduceFunction &&reduce, int size)
        : BaseType(futureInterface, begin, end, std::forward<MapFunction>(map), state,
                   std::forward<ReduceFunction>(reduce), size)
    {
    }

protected:
    void reduce(QFutureWatcher<void> *) override
    {
    }

};

template <typename ForwardIterator, typename InitFunction, typename MapFunction, typename ReduceResult,
          typename ReduceFunction, typename CleanUpFunction>
void blockingIteratorMapReduce(QFutureInterface<ReduceResult> &futureInterface, ForwardIterator begin, ForwardIterator end,
                               InitFunction &&init, MapFunction &&map,
                               ReduceFunction &&reduce, CleanUpFunction &&cleanup, int size)
{
    auto state = init(futureInterface);
    MapReduce<ForwardIterator, typename Internal::resultType<MapFunction>::type, MapFunction, decltype(state), ReduceResult, ReduceFunction>
            mr(futureInterface, begin, end, std::forward<MapFunction>(map), state,
               std::forward<ReduceFunction>(reduce), size);
    mr.exec();
    cleanup(futureInterface, state);
}

template <typename Container, typename InitFunction, typename MapFunction, typename ReduceResult,
          typename ReduceFunction, typename CleanUpFunction>
void blockingContainerMapReduce(QFutureInterface<ReduceResult> &futureInterface, Container &&container,
                                InitFunction &&init, MapFunction &&map,
                                ReduceFunction &&reduce, CleanUpFunction &&cleanup)
{
    blockingIteratorMapReduce(futureInterface, std::begin(container), std::end(container),
                              std::forward<InitFunction>(init), std::forward<MapFunction>(map),
                              std::forward<ReduceFunction>(reduce),
                              std::forward<CleanUpFunction>(cleanup), container.size());
}

template <typename Container, typename InitFunction, typename MapFunction, typename ReduceResult,
          typename ReduceFunction, typename CleanUpFunction>
void blockingContainerRefMapReduce(QFutureInterface<ReduceResult> &futureInterface,
                                    std::reference_wrapper<Container> containerWrapper,
                                    InitFunction &&init, MapFunction &&map,
                                    ReduceFunction &&reduce, CleanUpFunction &&cleanup)
{
    blockingContainerMapReduce(futureInterface, containerWrapper.get(),
                              std::forward<InitFunction>(init), std::forward<MapFunction>(map),
                              std::forward<ReduceFunction>(reduce),
                              std::forward<CleanUpFunction>(cleanup));
}

template <typename ReduceResult>
static void *dummyInit(QFutureInterface<ReduceResult> &) { return nullptr; }

template <typename MapResult>
struct DummyReduce {
    MapResult operator()(void *, const MapResult &result) const { return result; }
};
template <>
struct DummyReduce<void> {
    void operator()() const { } // needed for resultType<DummyReduce> with MSVC2013
};

template <typename ReduceResult>
static void dummyCleanup(QFutureInterface<ReduceResult> &, void *) { }

} // Internal

template <typename ForwardIterator, typename InitFunction, typename MapFunction,
          typename ReduceFunction, typename CleanUpFunction,
          typename ReduceResult = typename Internal::resultType<ReduceFunction>::type>
QFuture<ReduceResult>
mapReduce(ForwardIterator begin, ForwardIterator end, InitFunction &&init, MapFunction &&map,
               ReduceFunction &&reduce, CleanUpFunction &&cleanup, int size = -1)
{
    return runAsync(Internal::blockingIteratorMapReduce<
                        ForwardIterator,
                        typename std::decay<InitFunction>::type,
                        typename std::decay<MapFunction>::type,
                        typename std::decay<ReduceResult>::type,
                        typename std::decay<ReduceFunction>::type,
                        typename std::decay<CleanUpFunction>::type>,
                    begin, end, std::forward<InitFunction>(init), std::forward<MapFunction>(map),
                    std::forward<ReduceFunction>(reduce), std::forward<CleanUpFunction>(cleanup),
                    size);
}

/*!
    Calls the map function on all items in \a container in parallel through Utils::runAsync.

    The reduce function is called in the mapReduce thread with each of the reported results from
    the map function, in arbitrary order, but never in parallel.
    It gets passed a reference to a user defined state object, and a result from the map function.
    If it takes a QFutureInterface reference as its first argument, it can report results
    for the mapReduce operation through that. Otherwise, any values returned by the reduce function
    are reported as results of the mapReduce operation.

    The init function is called in the mapReduce thread before the actual mapping starts,
    and must return the initial state object for the reduce function. It gets the QFutureInterface
    of the mapReduce operation passed as an argument.

    The cleanup function is called in the mapReduce thread after all map and reduce calls have
    finished, with the QFutureInterface of the mapReduce operation and the final state object
    as arguments, and can be used to clean up any resources, or report a final result of the
    mapReduce.

    Container<ItemType>
    StateType InitFunction(QFutureInterface<ReduceResultType>&)

    void MapFunction(QFutureInterface<MapResultType>&, const ItemType&)
    or
    MapResultType MapFunction(const ItempType&)

    void ReduceFunction(QFutureInterface<ReduceResultType>&, StateType&, const ItemType&)
    or
    ReduceResultType ReduceFunction(StateType&, const ItemType&)

    void CleanUpFunction(QFutureInterface<ReduceResultType>&, StateType&)

    Notes:
    \list
        \li Container can be a move-only type or a temporary. If it is a lvalue reference, it will
            be copied to the mapReduce thread. You can avoid that by using
            the version that takes iterators, or by using std::ref/cref to pass a reference_wrapper.
        \li ItemType can be a move-only type, if the map function takes (const) references to ItemType.
        \li StateType can be a move-only type.
        \li The init, map, reduce and cleanup functions can be move-only types and are moved to the
            mapReduce thread if they are rvalues.
    \endlist

 */
template <typename Container, typename InitFunction, typename MapFunction,
          typename ReduceFunction, typename CleanUpFunction,
          typename ReduceResult = typename Internal::resultType<ReduceFunction>::type>
QFuture<ReduceResult>
mapReduce(Container &&container, InitFunction &&init, MapFunction &&map,
               ReduceFunction &&reduce, CleanUpFunction &&cleanup)
{
    return runAsync(Internal::blockingContainerMapReduce<
                        typename std::decay<Container>::type,
                        typename std::decay<InitFunction>::type,
                        typename std::decay<MapFunction>::type,
                        typename std::decay<ReduceResult>::type, typename std::decay<ReduceFunction>::type,
                        typename std::decay<CleanUpFunction>::type>,
                    std::forward<Container>(container),
                    std::forward<InitFunction>(init), std::forward<MapFunction>(map),
                    std::forward<ReduceFunction>(reduce), std::forward<CleanUpFunction>(cleanup));
}

template <typename Container, typename InitFunction, typename MapFunction,
          typename ReduceFunction, typename CleanUpFunction,
          typename ReduceResult = typename Internal::resultType<ReduceFunction>::type>
QFuture<ReduceResult>
mapReduce(std::reference_wrapper<Container> containerWrapper, InitFunction &&init, MapFunction &&map,
               ReduceFunction &&reduce, CleanUpFunction &&cleanup)
{
    return runAsync(Internal::blockingContainerRefMapReduce<
                        Container,
                        typename std::decay<InitFunction>::type,
                        typename std::decay<MapFunction>::type,
                        typename std::decay<ReduceResult>::type,
                        typename std::decay<ReduceFunction>::type,
                        typename std::decay<CleanUpFunction>::type>,
                    containerWrapper,
                    std::forward<InitFunction>(init), std::forward<MapFunction>(map),
                    std::forward<ReduceFunction>(reduce), std::forward<CleanUpFunction>(cleanup));
}

// TODO: Currently does not order its map results.
template <typename Container, typename MapFunction,
          typename MapResult = typename Internal::resultType<MapFunction>::type>
QFuture<MapResult>
map(Container &&container, MapFunction &&map)
{
    return mapReduce(std::forward<Container>(container),
                     Internal::dummyInit<MapResult>,
                     std::forward<MapFunction>(map),
                     Internal::DummyReduce<MapResult>(),
                     Internal::dummyCleanup<MapResult>);
}

} // Utils