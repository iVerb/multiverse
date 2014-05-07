//-*****************************************************************************
//
// Copyright (c) 2014,
//
// All rights reserved.
//
//-*****************************************************************************

#include <Alembic/AbcCoreGit/SpwImpl.h>
#include <Alembic/AbcCoreGit/CpwImpl.h>
#include <Alembic/AbcCoreGit/WriteUtil.h>
#include <Alembic/AbcCoreGit/Utils.h>

#include <iostream>
#include <fstream>
#include <json/json.h>

namespace Alembic {
namespace AbcCoreGit {
namespace ALEMBIC_VERSION_NS {

//-*****************************************************************************
SpwImpl::SpwImpl( AbcA::CompoundPropertyWriterPtr iParent,
                  GitGroupPtr iGroup,
                  PropertyHeaderPtr iHeader,
                  size_t iIndex ) :
    m_parent( iParent ), m_header( iHeader ),
    m_store( BuildSampleStore( iHeader->datatype(), /* rank-0 */ AbcA::Dimensions() ) ),
    m_group( iGroup ),
    m_index( iIndex ),
    m_written(false)
{
    ABCA_ASSERT( m_parent, "Invalid parent" );
    ABCA_ASSERT( m_header, "Invalid property header" );
    ABCA_ASSERT( m_group, "Invalid group" );

    TRACE("SpwImpl::SpwImpl(parent:" << CONCRETE_CPWPTR(m_parent)->repr() << ", group:" << m_group->repr() << ", header:'" << m_header->name() << "', index:" << m_index << ")");

    if ( m_header->header.getPropertyType() != AbcA::kScalarProperty )
    {
        ABCA_THROW( "Attempted to create a ScalarPropertyWriter from a "
                    "non-scalar property type" );
    }
}


//-*****************************************************************************
SpwImpl::~SpwImpl()
{
    AbcA::ArchiveWriterPtr archive = m_parent->getObject()->getArchive();

    index_t maxSamples = archive->getMaxNumSamplesForTimeSamplingIndex(
            m_header->timeSamplingIndex );

    Util::uint32_t numSamples = m_header->nextSampleIndex;

    // a constant property, we wrote the same sample over and over
    if ( m_header->lastChangedIndex == 0 && numSamples > 0 )
    {
        numSamples = 1;
    }

    if ( maxSamples < numSamples )
    {
        archive->setMaxNumSamplesForTimeSamplingIndex(
            m_header->timeSamplingIndex, numSamples );
    }

    Util::SpookyHash hash;
    hash.Init(0, 0);
    HashPropertyHeader( m_header->header, hash );

    // mix in the accumulated hash if we have samples
    if ( numSamples != 0 )
    {
        hash.Update( m_hash.d, 16 );
    }

    Util::uint64_t hash0, hash1;
    hash.Final( &hash0, &hash1 );
    Util::shared_ptr< CpwImpl > parent =
        Alembic::Util::dynamic_pointer_cast< CpwImpl,
            AbcA::CompoundPropertyWriter > ( m_parent );
    parent->fillHash( m_index, hash0, hash1 );

    writeToDisk();
}

//-*****************************************************************************
void SpwImpl::setFromPreviousSample()
{

    // Make sure we aren't writing more samples than we have times for
    // This applies to acyclic sampling only
    ABCA_ASSERT(
        !m_header->header.getTimeSampling()->getTimeSamplingType().isAcyclic()
        || m_header->header.getTimeSampling()->getNumStoredTimes() >
        m_header->nextSampleIndex,
        "Can not set more samples than we have times for when using "
        "Acyclic sampling." );

    ABCA_ASSERT( m_header->nextSampleIndex > 0,
        "Can't set from previous sample before any samples have been written" );

    ABCA_ASSERT( m_store.get(),
        "Can't set from previous sample before any samples have been written" );

    m_store->setFromPreviousSample();

    Util::Digest digest = m_previousWrittenSampleID->getKey().digest;
    Util::SpookyHash::ShortEnd(m_hash.words[0], m_hash.words[1],
                               digest.words[0], digest.words[1]);
    m_header->nextSampleIndex ++;
}

//-*****************************************************************************
void SpwImpl::setSample( const void *iSamp )
{
    // Make sure we aren't writing more samples than we have times for
    // This applies to acyclic sampling only
    ABCA_ASSERT(
        !m_header->header.getTimeSampling()->getTimeSamplingType().isAcyclic()
        || m_header->header.getTimeSampling()->getNumStoredTimes() >
        m_header->nextSampleIndex,
        "Can not write more samples than we have times for when using "
        "Acyclic sampling." );

    AbcA::ArraySample samp( iSamp, m_header->header.getDataType(),
                            AbcA::Dimensions(1) );

     // The Key helps us analyze the sample.
     AbcA::ArraySample::Key key = samp.getKey();

     // mask out the non-string POD since Git can safely share the same data
     // even if it originated from a different POD
     // the non-fixed sizes of our strings (plus added null characters) makes
     // determing the sie harder so strings are handled seperately
    if ( key.origPOD != Alembic::Util::kStringPOD &&
         key.origPOD != Alembic::Util::kWstringPOD )
    {
        key.origPOD = Alembic::Util::kInt8POD;
        key.readPOD = Alembic::Util::kInt8POD;
    }

    m_store->addSample( iSamp );

    // We need to write the sample
    UNIMPLEMENTED("WrittenSampleIDPtr m_previousWrittenSampleID");
#if 0
    if ( m_header->nextSampleIndex == 0  ||
        !( m_previousWrittenSampleID &&
            key == m_previousWrittenSampleID->getKey() ) )
    {

        // we only need to repeat samples if this is not the first change
        if (m_header->firstChangedIndex != 0)
        {
            // copy the samples from after the last change to the latest index
            for ( index_t smpI = m_header->lastChangedIndex + 1;
                smpI < m_header->nextSampleIndex; ++smpI )
            {
                assert( smpI > 0 );
                CopyWrittenData( m_group, m_previousWrittenSampleID );
            }
        }

        // Write this sample, which will update its internal
        // cache of what the previously written sample was.
        AbcA::ArchiveWriterPtr awp = this->getObject()->getArchive();

        // Write the sample.
        // This distinguishes between string, wstring, and regular arrays.
        m_previousWrittenSampleID =
            WriteData( GetWrittenSampleMap( awp ), m_group, samp, key );

        if (m_header->firstChangedIndex == 0)
        {
            m_header->firstChangedIndex = m_header->nextSampleIndex;
        }
        // this index is now the last change
        m_header->lastChangedIndex = m_header->nextSampleIndex;
    }

    if ( m_header->nextSampleIndex == 0 )
    {
        m_hash = m_previousWrittenSampleID->getKey().digest;
    }
    else
    {
        Util::Digest digest = m_previousWrittenSampleID->getKey().digest;
        Util::SpookyHash::ShortEnd( m_hash.words[0], m_hash.words[1],
                                    digest.words[0], digest.words[1] );
    }
#endif /* 0 */

    m_header->nextSampleIndex ++;
}

//-*****************************************************************************
AbcA::ScalarPropertyWriterPtr SpwImpl::asScalarPtr()
{
    return shared_from_this();
}

//-*****************************************************************************
size_t SpwImpl::getNumSamples()
{
    return ( size_t )m_header->nextSampleIndex;
}

//-*****************************************************************************
void SpwImpl::setTimeSamplingIndex( Util::uint32_t iIndex )
{
    // will assert if TimeSamplingPtr not found
    AbcA::TimeSamplingPtr ts =
        m_parent->getObject()->getArchive()->getTimeSampling( iIndex );

    ABCA_ASSERT( !ts->getTimeSamplingType().isAcyclic() ||
        ts->getNumStoredTimes() >= m_header->nextSampleIndex,
        "Already have written more samples than we have times for when using "
        "Acyclic sampling." );

    m_header->header.setTimeSampling(ts);
    m_header->timeSamplingIndex = iIndex;
}

//-*****************************************************************************
const AbcA::PropertyHeader & SpwImpl::getHeader() const
{
    ABCA_ASSERT( m_header, "Invalid header" );
    return m_header->header;
}

//-*****************************************************************************
AbcA::ObjectWriterPtr SpwImpl::getObject()
{
    ABCA_ASSERT( m_parent, "Invalid parent" );
    return m_parent->getObject();
}

//-*****************************************************************************
AbcA::CompoundPropertyWriterPtr SpwImpl::getParent()
{
    ABCA_ASSERT( m_parent, "Invalid parent" );
    return m_parent;
}

CpwImplPtr SpwImpl::getTParent() const
{
    Util::shared_ptr< CpwImpl > parent =
       Alembic::Util::dynamic_pointer_cast< CpwImpl,
        AbcA::CompoundPropertyWriter > ( m_parent );
    return parent;
}

std::string SpwImpl::repr(bool extended) const
{
    std::ostringstream ss;
    if (extended)
    {
        CpwImplPtr parentPtr = getTParent();

        ss << "<SpwImpl(parent:" << parentPtr->repr() << ", header:'" << m_header->name() << "', group:" << m_group->repr() << ")>";
    } else
    {
        ss << "'" << m_header->name() << "'";
    }
    return ss.str();
}

std::string SpwImpl::relPathname() const
{
    ABCA_ASSERT(m_group, "invalid group");

    std::string parent_path = m_group->relPathname();
    if (parent_path == "/")
    {
        return parent_path + name();
    } else
    {
        return parent_path + "/" + name();
    }
}

std::string SpwImpl::absPathname() const
{
    ABCA_ASSERT(m_group, "invalid group");

    std::string parent_path = m_group->absPathname();
    if (parent_path == "/")
    {
        return parent_path + name();
    } else
    {
        return parent_path + "/" + name();
    }
}

void SpwImpl::writeToDisk()
{
    if (! m_written)
    {
        TRACE("SpwImpl::writeToDisk() path:'" << absPathname() << "' (WRITING)");

        ABCA_ASSERT( m_group, "invalid group" );
        m_group->writeToDisk();

        TRACE("create '" << absPathname() << "'");

        ABCA_ASSERT( m_header->nextSampleIndex == m_store->getNumSamples(),
                     "invalid number of samples in SampleStore!" );

        Json::Value root( Json::objectValue );

//        const AbcA::MetaData& metaData = m_header->metadata();
        const AbcA::DataType& dataType = m_header->datatype();

        root["name"] = m_header->name();
        root["kind"] = "ScalarProperty";

        {
            std::ostringstream ss;
            ss << PODName( dataType.getPod() );
            root["typename"] = ss.str();
        }

        root["extent"] = dataType.getExtent();

        {
            std::ostringstream ss;
            ss << dataType;
            root["type"] = ss.str();
        }

        root["num_samples"] = TypedSampleStore<size_t>::JsonFromValue( m_store->getNumSamples() );

        root["dimensions"] = TypedSampleStore<AbcA::Dimensions>::JsonFromValue( m_store->getDimensions() );

        root["data"] = m_store->json();

        Json::Value propInfo( Json::objectValue );
        propInfo["isScalarLike"] = m_header->isScalarLike;
        propInfo["isHomogenous"] = m_header->isHomogenous;
        propInfo["timeSamplingIndex"] = m_header->timeSamplingIndex;
        propInfo["numSamples"] = m_header->nextSampleIndex;
        propInfo["firstChangedIndex"] = m_header->firstChangedIndex;
        propInfo["lastChangedIndex"] = m_header->lastChangedIndex;
        propInfo["metadata"] = m_header->header.getMetaData().serialize();

        root["info"] = propInfo;

        Json::StyledWriter writer;
        std::string output = writer.write( root );

        std::string jsonPathname = absPathname() + ".json";
        std::ofstream jsonFile;
        jsonFile.open(jsonPathname.c_str(), std::ios::out | std::ios::trunc);
        jsonFile << output;
        jsonFile.close();

        m_written = true;
    } else
    {
        TRACE("SpwImpl::writeToDisk() path:'" << absPathname() << "' (skipping, already written)");
    }

    ABCA_ASSERT( m_written, "data not written" );
}

} // End namespace ALEMBIC_VERSION_NS
} // End namespace AbcCoreGit
} // End namespace Alembic