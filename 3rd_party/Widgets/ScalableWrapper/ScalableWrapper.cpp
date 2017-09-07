#include "ScalableWrapper.h"

#include <3rd_party/Widgets/SpellCheckTextEdit/SpellCheckTextEdit.h>
#include <3rd_party/Widgets/CompletableTextEdit/CompletableTextEdit.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QGestureEvent>
#include <QGraphicsProxyWidget>
#include <QMenu>
#include <QScreen>
#include <QScrollBar>
#include <QScroller>
#include <QShortcut>
#include <QTextEdit>
#include <QTimer>

namespace {
    const qreal DEFAULT_ZOOM_RANGE = 1.;
    const qreal MINIMUM_ZOOM_RANGE = 0.5;
    const qreal MAXIMUM_ZOOM_RANGE = 3.;
}


ScalableWrapper::ScalableWrapper(SpellCheckTextEdit* _editor, QWidget* _parent) :
    QGraphicsView(_parent),
    m_scene(new QGraphicsScene),
    m_editor(_editor)
{
    //
    // Настраиваем лучшее опции прорисовки
    //
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(AnchorUnderMouse);

    //
    // Отслеживаем жесты
    //
    grabGesture(Qt::PinchGesture);
    grabGesture(Qt::SwipeGesture);

    //
    // Предварительная настройка редактора текста
    //
    // FIXME: непонятно как быть с предком, у встраиваемого виджета не должно быть родителя,
    //		  но как в таком случае освобождать память?
    //
    m_editor->setParent(0);
    m_editor->setContextMenuPolicy(Qt::PreventContextMenu);
    m_editor->installEventFilter(this);
    m_editor->setFrameShape(QFrame::NoFrame);

    //
    // Настраиваем само представление
    //
    m_rect = m_scene->addRect(0, 0, 1, 1, QPen(), Qt::transparent);
    m_editorProxy = m_scene->addWidget(m_editor);
    m_editorProxy->setPos(0, 0);
    setScene(m_scene);

    //
    // Отключаем действия полос прокрутки, чтобы в дальнейшем проксировать ими
    // полосы прокрутки самого редактора текста
    //
    horizontalScrollBar()->disconnect();
    verticalScrollBar()->disconnect();

    //
    // Синхронизация значения ролика в обе стороны
    //
    setupScrollingSynchronization(true);

    //
    // Добавляем возможность масштабирования при помощи комбинаций Ctrl +/-
    //
    QShortcut* zoomInShortcut1 = new QShortcut(QKeySequence("Ctrl++"), this, 0, 0, Qt::WidgetShortcut);
    connect(zoomInShortcut1, SIGNAL(activated()), this, SLOT(zoomIn()));
    QShortcut* zoomInShortcut2 = new QShortcut(QKeySequence("Ctrl+="), this, 0, 0, Qt::WidgetShortcut);
    connect(zoomInShortcut2, SIGNAL(activated()), this, SLOT(zoomIn()));
    QShortcut* zoomOutShortcut = new QShortcut(QKeySequence("Ctrl+-"), this, 0, 0, Qt::WidgetShortcut);
    connect(zoomOutShortcut, SIGNAL(activated()), this, SLOT(zoomOut()));

    if (CompletableTextEdit* editor = qobject_cast<CompletableTextEdit*>(m_editor)) {
        connect(editor, &CompletableTextEdit::popupShowed, [=] {
            QPointF point = m_editorProxy->mapToScene(editor->completer()->popup()->pos());
            editor->completer()->popup()->move(mapToGlobal(mapFromScene(point)));
        });
        connect(editor, &CompletableTextEdit::cursorPositionChanged, this, &ScalableWrapper::cursorPositionChanged);
    }
}

SpellCheckTextEdit* ScalableWrapper::editor() const
{
    return m_editor;
}

void ScalableWrapper::setZoomRange(qreal _zoomRange)
{
    if (m_zoomRange != _zoomRange
        && _zoomRange >= MINIMUM_ZOOM_RANGE
        && _zoomRange <= MAXIMUM_ZOOM_RANGE) {
        m_zoomRange = _zoomRange;
        emit zoomRangeChanged(m_zoomRange);

        scaleTextEdit();
        updateTextEditSize();
    }
}

QVariant ScalableWrapper::inputMethodQuery(Qt::InputMethodQuery _query) const
{
    return inputMethodQuery(_query, QVariant());
}

QVariant ScalableWrapper::inputMethodQuery(Qt::InputMethodQuery _query, QVariant _argument) const
{
    QVariant result;
    if (m_editor != nullptr) {
        result = m_editor->inputMethodQuery(_query, _argument);

        //
        // FIXME: Недоделанный индикатор выделения
        //
//        //
//        // Масштабируем результат
//        //
//        if (true) {
//            switch (result.type()) {
//                case QVariant::RectF: {
//                    result = QRectF(result.toRectF().topLeft() * m_zoomRange,
//                                    result.toRectF().size() * m_zoomRange);
//                    break;
//                }

//                case QVariant::PointF: {
//                    result = result.toPointF() * m_zoomRange;
//                    break;
//                }

//                case QVariant::Rect: {
//                    result = QRect(result.toRect().topLeft() * m_zoomRange,
//                                   result.toRect().size() * m_zoomRange);
//                    break;
//                }

//                case QVariant::Point: {
//                    result = result.toPoint() * m_zoomRange;
//                    break;
//                }

//                default: break;
//            }
//        }
    } else {
        result = QGraphicsView::inputMethodQuery(_query);
    }
    
#ifdef Q_OS_IOS
    //
    // Делаем курсор всегда на нуле, чтобы редактор сценария не выкидывало ни вниз ни вверх
    //
    if (_query & Qt::ImCursorRectangle) {
        result = QRectF(0,0,0,0);
    }
    //
    // ... и чтобы удаление работало корректно
    //
    else if (_query & Qt::ImCursorPosition) {
        result = QVariant();
    }
#endif
    return result;
}

void ScalableWrapper::zoomIn()
{
    setZoomRange(m_zoomRange + 0.1);
}

void ScalableWrapper::zoomOut()
{
    setZoomRange(m_zoomRange - 0.1);
}

bool ScalableWrapper::event(QEvent* _event)
{
    bool result = true;
    //
    // Определяем особый обработчик для жестов
    //
    if (_event->type() == QEvent::Gesture) {
        gestureEvent(static_cast<QGestureEvent*>(_event));
    }
    //
    // Для событий showEvent и resizeEvent отключаем синхронизацию полос прокрутки,
    // т.к. в стандартной реализации QGraphicsView они сбиваются для нас
    //
    else if (_event->type() == QEvent::Show
             || _event->type() == QEvent::Resize) {
        //
        // Перед событием отключаем синхронизацию полос прокрутки
        //

        setupScrollingSynchronization(false);

        result = QGraphicsView::event(_event);

        updateTextEditSize();

        //
        // Корректируем размер сцены, чтобы исключить внезапные смещения редактора на ней
        //
        if (m_scene->sceneRect() != viewport()->rect()) {
            setSceneRect(viewport()->rect());
            ensureVisible(m_editorProxy);
            syncScrollBarWithTextEdit();
        }

        //
        // А после события включаем синхронизацию
        //
        setupScrollingSynchronization(true);
    }
    //
    // Во время события paint корректируем размер встроенного редактора
    //
    else if (_event->type() == QEvent::Paint) {
        updateTextEditSize();

        result = QGraphicsView::event(_event);
    }
    //
    // После события обновления компоновки, полностью перенастраиваем полосы прокрутки
    //
    else if (_event->type() == QEvent::LayoutRequest) {
        setupScrollingSynchronization(false);

        result = QGraphicsView::event(_event);

        syncScrollBarWithTextEdit();
        setupScrollingSynchronization(true);
    }
    //
    // FIXME: Недоделанный индикатор выделения
    //
//    else if (_event->type() == QEvent::InputMethodQuery) {
//        auto printEventData = [this] (QEvent* ev, bool ch) {
//            if (ev->type() != QEvent::InputMethodQuery)
//                return;
//            qDebug() << ev;
//            if (QInputMethodQueryEvent* e = static_cast<QInputMethodQueryEvent*>(ev)) {
//                qDebug() << e << (e->queries() == Qt::ImCursorRectangle) << (e->queries() == Qt::ImAnchorRectangle);
//                if (e->queries().testFlag(Qt::ImCursorPosition)) {
//                    qDebug() << Qt::ImCursorPosition << e->value(Qt::ImCursorPosition);
//                }
//                if (e->queries().testFlag(Qt::ImCursorRectangle)) {
//                    qDebug() << Qt::ImCursorRectangle << e->value(Qt::ImCursorRectangle);
//                    QRectF rect =  e->value(Qt::ImCursorRectangle).toRectF();
//                    if (ch)
//                    {
//                        e->setValue(Qt::ImCursorRectangle, QRectF(rect.topLeft() * m_zoomRange,
//                                                                  rect.size() * m_zoomRange));
//                        qDebug() << Qt::ImCursorRectangle << e->value(Qt::ImCursorRectangle);
//                    }
//                }
//                if (e->queries().testFlag(Qt::ImAnchorPosition)) {
//                    qDebug() << Qt::ImAnchorPosition << e->value(Qt::ImAnchorPosition);
//                }
//                if (e->queries().testFlag(Qt::ImAnchorRectangle)) {
//                    qDebug() << Qt::ImAnchorRectangle << e->value(Qt::ImAnchorRectangle);
//                    QRectF rect =  e->value(Qt::ImAnchorRectangle).toRectF();
//                    if (ch)
//                    {
//                        e->setValue(Qt::ImAnchorRectangle, QRectF(rect.topLeft() * m_zoomRange,
//                                                                  rect.size() * m_zoomRange));
//                        qDebug() << Qt::ImAnchorRectangle << e->value(Qt::ImAnchorRectangle);
//                    }
//                }
//            }
//        };

//        qDebug() << "*********";
//        result = QGraphicsView::event(_event);
//        printEventData(_event, false);
//        qDebug() << "===========";
//        if (QInputMethodQueryEvent* e = static_cast<QInputMethodQueryEvent*>(_event)) {
//            if (e->queries() != Qt::ImCursorRectangle
//                && e->queries() != Qt::ImAnchorRectangle);
//                result = QApplication::sendEvent(m_editor, _event);
//        }
////        printEventData(_event, false);
//    }
    //
    // Прочие стандартные обработчики событий
    //
    else {
        result = QGraphicsView::event(_event);

        //
        // Переустанавливаем фокус в редактор, иначе в нём пропадает курсор
        //
        if (_event->type() == QEvent::FocusIn) {
            m_editor->clearFocus();
            m_editor->setFocus();
        }
    }

    return result;
}

void ScalableWrapper::wheelEvent(QWheelEvent* _event)
{
#ifdef Q_OS_MAC
    const qreal ANGLE_DIVIDER = 2.;
#else
    const qreal ANGLE_DIVIDER = 120.;
#endif
    const qreal ZOOM_COEFFICIENT_DIVIDER = 10.;

    //
    // Собственно масштабирование
    //
    if (_event->modifiers() & Qt::ControlModifier) {
        if (_event->orientation() == Qt::Vertical) {
            //
            // zoomRange > 0 - масштаб увеличивается
            // zoomRange < 0 - масштаб уменьшается
            //
            const qreal zoom = _event->angleDelta().y() / ANGLE_DIVIDER;
            setZoomRange(m_zoomRange + zoom / ZOOM_COEFFICIENT_DIVIDER);

            _event->accept();
        }
    }
    //
    // В противном случае прокручиваем редактор
    //
    else {
        QGraphicsView::wheelEvent(_event);
    }
}

void ScalableWrapper::gestureEvent(QGestureEvent* _event)
{
    //
    // Жест масштабирования
    //
    if (QGesture* gesture = _event->gesture(Qt::PinchGesture)) {
        if (QPinchGesture* pinch = qobject_cast<QPinchGesture *>(gesture)) {
            //
            // При масштабировании за счёт жестов приходится немного притормаживать
            // т.к. события приходят слишком часто и при обработке каждого события
            // пользователю просто невозможно корректно настроить масштаб
            //

            const int INERTION_BREAK_STOP = 8;
            qreal zoomDelta = 0;
            if (pinch->scaleFactor() > 1) {
                if (m_gestureZoomInertionBreak < 0) {
                    m_gestureZoomInertionBreak = 0;
                } else if (m_gestureZoomInertionBreak >= INERTION_BREAK_STOP) {
                    m_gestureZoomInertionBreak = 0;
                    zoomDelta = 0.1;
                } else {
                    ++m_gestureZoomInertionBreak;
                }
            } else if (pinch->scaleFactor() < 1) {
                if (m_gestureZoomInertionBreak > 0) {
                    m_gestureZoomInertionBreak = 0;
                } else if (m_gestureZoomInertionBreak <= -INERTION_BREAK_STOP) {
                    m_gestureZoomInertionBreak = 0;
                    zoomDelta = -0.1;
                } else {
                    --m_gestureZoomInertionBreak;
                }
            } else {
                //
                // При обычной прокрутке часто приходит событие с scaledFactor == 1,
                // так что просто игнорируем их
                //
            }

            //
            // Если необходимо масштабируем и перерисовываем представление
            //
            if (zoomDelta != 0) {
                setZoomRange(m_zoomRange + zoomDelta);
            }

            _event->accept();
        }
    }
}

//
// FIXME: Недоделанный индикатор выделения
//
//void ScalableWrapper::mouseReleaseEvent(QMouseEvent* _event)
//{
//    QGraphicsView::mouseReleaseEvent(_event);

////    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle
////#if QT_VERSION >= 0x050900
////                                           | Qt::ImAnchorRectangle
////#endif
////                                           );
//}

//void ScalableWrapper::mouseDoubleClickEvent(QMouseEvent* _event)
//{
//    QGraphicsView::mouseDoubleClickEvent(_event);

////    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle
////#if QT_VERSION >= 0x050900
////                                           | Qt::ImAnchorRectangle
////#endif
////                                           );
//}

//void ScalableWrapper::inputMethodEvent(QInputMethodEvent* _event)
//{
////    QApplication::sendEvent(m_editor, _event);
//    QGraphicsView::inputMethodEvent(_event);

////    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle
////#if QT_VERSION >= 0x050900
////                                           | Qt::ImAnchorRectangle
////#endif
////                                           );
//}

bool ScalableWrapper::eventFilter(QObject* _object, QEvent* _event)
{
    bool needShowMenu = false;
    QPoint cursorGlobalPos = QCursor::pos();
    switch (_event->type()) {
        case QEvent::ContextMenu: {
            QContextMenuEvent* contextMenuEvent = static_cast<QContextMenuEvent*>(_event);
            cursorGlobalPos = contextMenuEvent->globalPos();
            needShowMenu = true;
            break;
        }

        case QEvent::MouseButtonPress: {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(_event);
            if (mouseEvent->button() == Qt::RightButton) {
                cursorGlobalPos = mouseEvent->globalPos();
                needShowMenu = true;
            }
            break;
        }

        default: {
            break;
        }
    }

    bool result = false;

    //
    // Если необходимо, то показываем контекстное меню
    //
    if (needShowMenu) {
        QMenu* menu = m_editor->createContextMenu(m_editor->viewport()->mapFromGlobal(cursorGlobalPos), this);
        menu->exec(QCursor::pos());
        delete menu;

        m_editor->clearFocus();
        m_editor->setFocus();

        //
        // Событие перехвачено
        //
        result = true;
    }
    //
    // Если нет, то стандартная обработка события
    //
    else {
        //
        // Возвращаем фокус редактору, если он его потерял
        //
        if (_object == m_editor
            && _event->type() == QEvent::FocusOut) {
            m_editor->clearFocus();
            m_editor->setFocus();
        }

        result = QGraphicsView::eventFilter(_object, _event);
    }

    return result;
}

void ScalableWrapper::setupScrollingSynchronization(bool _needSync)
{
    m_isScrollingSynchronizationActive = _needSync;

    if (_needSync) {
        connect(m_editor->verticalScrollBar(), &QScrollBar::rangeChanged, this, &ScalableWrapper::updateTextEditSize);
        connect(m_editor->horizontalScrollBar(), &QScrollBar::rangeChanged, this, &ScalableWrapper::updateTextEditSize);
        //
        connect(verticalScrollBar(), SIGNAL(valueChanged(int)),
                m_editor->verticalScrollBar(), SLOT(setValue(int)));
        connect(horizontalScrollBar(), SIGNAL(valueChanged(int)),
                m_editor->horizontalScrollBar(), SLOT(setValue(int)));
        // --
        connect(m_editor->verticalScrollBar(), SIGNAL(valueChanged(int)),
                verticalScrollBar(), SLOT(setValue(int)));
        connect(m_editor->horizontalScrollBar(), SIGNAL(valueChanged(int)),
                horizontalScrollBar(), SLOT(setValue(int)));
    } else {
        disconnect(m_editor->verticalScrollBar(), &QScrollBar::rangeChanged, this, &ScalableWrapper::updateTextEditSize);
        disconnect(m_editor->horizontalScrollBar(), &QScrollBar::rangeChanged, this, &ScalableWrapper::updateTextEditSize);
        //
        disconnect(verticalScrollBar(), SIGNAL(valueChanged(int)),
                m_editor->verticalScrollBar(), SLOT(setValue(int)));
        disconnect(horizontalScrollBar(), SIGNAL(valueChanged(int)),
                m_editor->horizontalScrollBar(), SLOT(setValue(int)));
        // --
        disconnect(m_editor->verticalScrollBar(), SIGNAL(valueChanged(int)),
                verticalScrollBar(), SLOT(setValue(int)));
        disconnect(m_editor->horizontalScrollBar(), SIGNAL(valueChanged(int)),
                horizontalScrollBar(), SLOT(setValue(int)));
    }
}

void ScalableWrapper::syncScrollBarWithTextEdit(bool _syncPosition)
{
    //
    // Скорректируем размерность полос прокрутки
    //
    const int rectHeight = m_editor->verticalScrollBar()->maximum();
    const int rectWidth = m_editor->horizontalScrollBar()->maximum();
    if (verticalScrollBar()->maximum() != rectHeight) {
        m_rect->setRect(0, 0, rectWidth, rectHeight);
        verticalScrollBar()->setMaximum(rectHeight);
        horizontalScrollBar()->setMaximum(rectWidth);
    }

    //
    // Скорректируем положение полос прокрутки
    //
    if (_syncPosition) {
        verticalScrollBar()->setValue(m_editor->verticalScrollBar()->value());
        horizontalScrollBar()->setValue(m_editor->horizontalScrollBar()->value());
    }
}

void ScalableWrapper::updateTextEditSize()
{
    //
    // Задаём политику отображения полосы прокрутки.
    // При смене необходимо отключать синхронизацию, если она была активирована,
    // чтобы не происходило отбрасывание в нулевую позицию соседнего скролбара
    //
    auto setScrollBarVisibility = [=] (bool _isVerticalScrollBar, Qt::ScrollBarPolicy _policy) {
        const bool needSync = m_isScrollingSynchronizationActive;
        if (needSync) {
            setupScrollingSynchronization(false);
        }

        if (_isVerticalScrollBar
            && verticalScrollBarPolicy() != _policy) {
            setVerticalScrollBarPolicy(_policy);
        } else if (!_isVerticalScrollBar
                   && horizontalScrollBarPolicy() != _policy) {
            setHorizontalScrollBarPolicy(_policy);
        }

        if (needSync) {
            setupScrollingSynchronization(true);
        }
    };

    int vbarWidth = 0;
    const bool VERTICAL_SCROLLBAR = true;
    const bool HORIZONTAL_SCROLLBAR = false;
    if (m_editor->verticalScrollBar()->isVisible()) {
        vbarWidth = m_editor->verticalScrollBar()->width();
        setScrollBarVisibility(VERTICAL_SCROLLBAR, Qt::ScrollBarAlwaysOn);
    } else {
        setScrollBarVisibility(VERTICAL_SCROLLBAR, Qt::ScrollBarAlwaysOff);
    }
    //
    int hbarHeight = 0;
    if (m_editor->horizontalScrollBar()->isVisible()) {
        hbarHeight = m_editor->horizontalScrollBar()->height();
        setScrollBarVisibility(HORIZONTAL_SCROLLBAR, Qt::ScrollBarAlwaysOn);
    } else {
        setScrollBarVisibility(HORIZONTAL_SCROLLBAR, Qt::ScrollBarAlwaysOff);
    }

    //
    // Размер редактора устанавливается таким образом, чтобы спрятать масштабированные полосы
    // прокрутки (скрывать их нельзя, т.к. тогда теряются значения, которые необходимо проксировать)
    //
    const int scrollBarsDiff = 2; // Считаем, что скролбар редактора на 2 пикселя шире скролбара обёртки
    const int editorWidth = width() / m_zoomRange + vbarWidth + m_zoomRange - scrollBarsDiff;
    const int editorHeight = height() / m_zoomRange + hbarHeight + m_zoomRange - scrollBarsDiff;
    const QSize editorSize(editorWidth, editorHeight);
    if (m_editorProxy->size() != editorSize) {
        m_editorProxy->resize(editorSize);
    }

    //
    // Необходимые действия для корректировки значений на полосах прокрутки
    //
    const bool dontSyncScrollPosition = false;
    syncScrollBarWithTextEdit(dontSyncScrollPosition);
}

void ScalableWrapper::scaleTextEdit()
{
    if (m_zoomRange == 0) {
        m_zoomRange = DEFAULT_ZOOM_RANGE;
    } else if (m_zoomRange < MINIMUM_ZOOM_RANGE) {
        m_zoomRange = MINIMUM_ZOOM_RANGE;
    } else if (m_zoomRange > MAXIMUM_ZOOM_RANGE) {
        m_zoomRange = MAXIMUM_ZOOM_RANGE;
    }
    m_editorProxy->setScale(m_zoomRange);
}

