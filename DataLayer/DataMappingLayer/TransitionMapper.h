#ifndef TRANSITIONMAPPER_H
#define TRANSITIONMAPPER_H

#include "AbstractMapper.h"
#include "MapperFacade.h"

namespace Domain {
    class Transition;
    class TransitionsTable;
}

using namespace Domain;


namespace DataMappingLayer
{
    class TransitionMapper : public AbstractMapper
    {
    public:
        Transition* find(const Identifier& _id);
        TransitionsTable* findAll();
        void insert(Transition* _transition);
        void update(Transition* _transition);

    protected:
        QString findStatement(const Identifier& _id) const;
        QString findAllStatement() const;
        QString insertStatement(DomainObject* _subject, QVariantList& _insertValues) const;
        QString updateStatement(DomainObject* _subject, QVariantList& _updateValues) const;
        QString deleteStatement(DomainObject* _subject, QVariantList& _deleteValues) const;

    protected:
        DomainObject* doLoad(const Identifier& _id, const QSqlRecord& _record);
        void doLoad(DomainObject* _domainObject, const QSqlRecord& _record);
        DomainObjectsItemModel* modelInstance();

    private:
        TransitionMapper();

        // Для доступа к конструктору
        friend class MapperFacade;
    };
}

#endif // TRANSITIONMAPPER_H
