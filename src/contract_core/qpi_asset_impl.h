#pragma once

#include "contracts/qpi.h"

#include "assets/assets.h"
#include "../spectrum.h"


// Start iteration with given issuance and given ownership filter (selects first record).
void QPI::AssetOwnershipIterator::begin(const QPI::AssetIssuanceId& issuance, const QPI::AssetOwnershipSelect& ownership)
{
    _issuance = issuance;
    _issuanceIdx = ::issuanceIndex(issuance.issuer, issuance.assetName);
    _ownership = ownership;
    _ownershipIdx = NO_ASSET_INDEX;

    if (_issuanceIdx == NO_ASSET_INDEX)
        return;

    next();
}

// Return if iteration with next() has reached end.
bool QPI::AssetOwnershipIterator::reachedEnd() const
{
    ASSERT(!(_issuanceIdx == NO_ASSET_INDEX && _ownershipIdx != NO_ASSET_INDEX));
    return _ownershipIdx == NO_ASSET_INDEX;
}

// Step to next ownership record matching filtering criteria.
bool QPI::AssetOwnershipIterator::next()
{
    ASSERT(_issuanceIdx < ASSETS_CAPACITY);

    if (!_ownership.anyOwner)
    {
        // searching for specific owner -> use hash map
        if (_ownershipIdx == NO_ASSET_INDEX)
        {
            // get first candidate for ownership of issuance in first call of next()
            _ownershipIdx = _ownership.owner.m256i_u32[0] & (ASSETS_CAPACITY - 1);
        }
        else
        {
            // get next candidate in following calls of next()
            _ownershipIdx = (_ownershipIdx + 1) & (ASSETS_CAPACITY - 1);
        }

        // search entry in consecutive non-empty fields of hash map
        while (assets[_ownershipIdx].varStruct.ownership.type != EMPTY)
        {
            if (assets[_ownershipIdx].varStruct.ownership.type == OWNERSHIP
                && assets[_ownershipIdx].varStruct.ownership.issuanceIndex == _issuanceIdx
                && assets[_ownershipIdx].varStruct.ownership.publicKey == _ownership.owner
                && (_ownership.anyManagingContract || assets[_ownershipIdx].varStruct.ownership.managingContractIndex == _ownership.managingContract))
            {
                // found matching entry
                return true;
            }

            _ownershipIdx = (_ownershipIdx + 1) & (ASSETS_CAPACITY - 1);
        }

        // no matching entry found
        _ownershipIdx = NO_ASSET_INDEX;
        return false;
    }
    else
    {
        // owner is unknow -> use index lists instead of hash map to iterate through ownerships belonging to issuance
        if (_ownershipIdx == NO_ASSET_INDEX)
        {
            // get first ownership of issuance
            _ownershipIdx = as.indexLists.ownnershipsPossessionsFirstIdx[_issuanceIdx];
            ASSERT(_ownershipIdx == NO_ASSET_INDEX
                || (_ownershipIdx < ASSETS_CAPACITY
                    && assets[_ownershipIdx].varStruct.ownership.type == OWNERSHIP
                    && assets[_ownershipIdx].varStruct.ownership.issuanceIndex == _issuanceIdx));
        }
        else
        {
            // get next ownership
            _ownershipIdx = as.indexLists.nextIdx[_ownershipIdx];
            ASSERT(_ownershipIdx == NO_ASSET_INDEX
                || (_ownershipIdx < ASSETS_CAPACITY
                    && assets[_ownershipIdx].varStruct.ownership.type == OWNERSHIP
                    && assets[_ownershipIdx].varStruct.ownership.issuanceIndex == _issuanceIdx));
        }

        // if specific managing contract is requested, make sure the ownership matches
        if (!_ownership.anyManagingContract)
        {
            while (_ownershipIdx != NO_ASSET_INDEX
                && assets[_ownershipIdx].varStruct.ownership.managingContractIndex != _ownership.managingContract)
            {
                _ownershipIdx = as.indexLists.nextIdx[_ownershipIdx];
                ASSERT(_ownershipIdx == NO_ASSET_INDEX
                    || (_ownershipIdx < ASSETS_CAPACITY
                        && assets[_ownershipIdx].varStruct.ownership.type == OWNERSHIP
                        && assets[_ownershipIdx].varStruct.ownership.issuanceIndex == _issuanceIdx));
            }
        }

        return _ownershipIdx != NO_ASSET_INDEX;
    }
}

id QPI::AssetOwnershipIterator::issuer() const
{
    ASSERT(_issuanceIdx == NO_ASSET_INDEX || (_issuanceIdx < ASSETS_CAPACITY && assets[_issuanceIdx].varStruct.issuance.type == ISSUANCE));
    return (_issuanceIdx < ASSETS_CAPACITY) ? assets[_issuanceIdx].varStruct.issuance.publicKey : id::zero();
}

id QPI::AssetOwnershipIterator::owner() const
{
    ASSERT(_ownershipIdx == NO_ASSET_INDEX || (_ownershipIdx < ASSETS_CAPACITY && assets[_ownershipIdx].varStruct.ownership.type == OWNERSHIP));
    return (_ownershipIdx < ASSETS_CAPACITY) ? assets[_ownershipIdx].varStruct.ownership.publicKey : id::zero();
}

sint64 QPI::AssetOwnershipIterator::numberOfOwnedShares() const
{
    ASSERT(_ownershipIdx == NO_ASSET_INDEX || (_ownershipIdx < ASSETS_CAPACITY && assets[_ownershipIdx].varStruct.ownership.type == OWNERSHIP));
    return (_ownershipIdx < ASSETS_CAPACITY) ? assets[_ownershipIdx].varStruct.ownership.numberOfShares : -1;
}


// Start iteration with given issuance and given ownership + possession filters (selects first record).
void QPI::AssetPossessionIterator::begin(const AssetIssuanceId& issuance, const AssetOwnershipSelect& ownership, const AssetPossessionSelect& possession)
{
    AssetOwnershipIterator::begin(issuance, ownership);

    _possession = possession;
    _possessionIdx = NO_ASSET_INDEX;

    if (_issuanceIdx == NO_ASSET_INDEX)
        return;

    next();
}

// Return if iteration with next() has reached end.
bool QPI::AssetPossessionIterator::reachedEnd() const
{
    ASSERT(
        (_possessionIdx != NO_ASSET_INDEX && _ownershipIdx != NO_ASSET_INDEX && _issuanceIdx != NO_ASSET_INDEX) ||
        (_possessionIdx == NO_ASSET_INDEX && _ownershipIdx != NO_ASSET_INDEX && _issuanceIdx != NO_ASSET_INDEX) ||
        (_possessionIdx == NO_ASSET_INDEX && _ownershipIdx == NO_ASSET_INDEX && _issuanceIdx != NO_ASSET_INDEX) ||
        (_possessionIdx == NO_ASSET_INDEX && _ownershipIdx == NO_ASSET_INDEX && _issuanceIdx == NO_ASSET_INDEX)
    );
    return _possessionIdx == NO_ASSET_INDEX;
}

// Step to next possession record matching filtering criteria.
bool QPI::AssetPossessionIterator::next()
{
    ASSERT(_issuanceIdx < ASSETS_CAPACITY && _ownershipIdx < ASSETS_CAPACITY);

    if (!_possession.anyPossessor)
    {
        // searching for specific possessor -> use hash map

        // TODO: the common case of specific possessor and don't care about ownership should be optimized from O(N^2) to O(N)
        do
        {
            if (_possessionIdx == NO_ASSET_INDEX)
            {
                _possessionIdx = _possession.possessor.m256i_u32[0] & (ASSETS_CAPACITY - 1);
            }
            else
            {
                _possessionIdx = (_possessionIdx + 1) & (ASSETS_CAPACITY - 1);
            }
            while (assets[_possessionIdx].varStruct.possession.type != EMPTY)
            {
                if (assets[_possessionIdx].varStruct.possession.type == POSSESSION
                    && assets[_possessionIdx].varStruct.possession.ownershipIndex == _ownershipIdx
                    && assets[_possessionIdx].varStruct.possession.publicKey == _possession.possessor
                    && (_possession.anyManagingContract || assets[_possessionIdx].varStruct.possession.managingContractIndex == _possession.managingContract))
                {
                    // found matching entry
                    return true;
                }

                _possessionIdx = (_possessionIdx + 1) & (ASSETS_CAPACITY - 1);
            }

            // no matching entry found
            _possessionIdx = NO_ASSET_INDEX;

            // retry with next owner
        } while (AssetOwnershipIterator::next());
    }
    else
    {
        // possessor is unknow -> use index lists instead of hash map to iterate through possessions belonging to ownership
        do
        {
            if (_possessionIdx == NO_ASSET_INDEX)
            {
                // get first possession of ownership
                _possessionIdx = as.indexLists.ownnershipsPossessionsFirstIdx[_ownershipIdx];
                ASSERT(_possessionIdx == NO_ASSET_INDEX
                    || (_possessionIdx < ASSETS_CAPACITY
                        && assets[_possessionIdx].varStruct.possession.type == POSSESSION
                        && assets[_possessionIdx].varStruct.possession.ownershipIndex == _ownershipIdx));
            }
            else
            {
                // get next ownership
                _possessionIdx = as.indexLists.nextIdx[_possessionIdx];
                ASSERT(_possessionIdx == NO_ASSET_INDEX
                    || (_possessionIdx < ASSETS_CAPACITY
                        && assets[_possessionIdx].varStruct.possession.type == POSSESSION
                        && assets[_possessionIdx].varStruct.possession.ownershipIndex == _ownershipIdx));
            }

            // if specific managing contract is requested, make sure the possession matches
            if (!_possession.anyManagingContract)
            {
                while (_possessionIdx != NO_ASSET_INDEX
                    && assets[_possessionIdx].varStruct.possession.managingContractIndex != _possession.managingContract)
                {
                    _possessionIdx = as.indexLists.nextIdx[_possessionIdx];
                    ASSERT(_possessionIdx == NO_ASSET_INDEX
                        || (_possessionIdx < ASSETS_CAPACITY
                            && assets[_possessionIdx].varStruct.possession.type == POSSESSION
                            && assets[_possessionIdx].varStruct.possession.ownershipIndex == _ownershipIdx));
                }
            }

            if (_possessionIdx != NO_ASSET_INDEX)
                return true;

            // no matching entry found -> retry with next owner
        } while (AssetOwnershipIterator::next());
    }

    ASSERT(_ownershipIdx == NO_ASSET_INDEX && _possessionIdx == NO_ASSET_INDEX);

    return false;
}

inline id QPI::AssetPossessionIterator::possessor() const
{
    ASSERT(_possessionIdx == NO_ASSET_INDEX || (_possessionIdx < ASSETS_CAPACITY && assets[_possessionIdx].varStruct.possession.type == POSSESSION));
    return (_possessionIdx < ASSETS_CAPACITY) ? assets[_possessionIdx].varStruct.possession.publicKey : id::zero();
}

sint64 QPI::AssetPossessionIterator::numberOfPossessedShares() const
{
    ASSERT(_possessionIdx == NO_ASSET_INDEX || (_possessionIdx < ASSETS_CAPACITY && assets[_possessionIdx].varStruct.possession.type == POSSESSION));
    return (_possessionIdx < ASSETS_CAPACITY) ? assets[_possessionIdx].varStruct.possession.numberOfShares : -1;
}


////////////////////////////


bool QPI::QpiContextProcedureCall::acquireShares(uint64 assetName, const id& issuer, const id& owner, const id& possessor, sint64 numberOfShares, uint16 sourceOwnershipManagingContractIndex, uint16 sourcePossessionManagingContractIndex) const
{
    // Just examples, to make it compile, move these to parameter list
    unsigned int contractIndex = QX_CONTRACT_INDEX;
    QPI::sint64 invocationReward = 10;

    if (contractIndex >= contractCount)
        return false;
    if (invocationReward < 0)
        return false;
    // ...

    // TODO: Init input
    QPI::PreManagementRightsTransfer_input pre_input;
    // output is zeroed in __qpiCallSystemProcOfOtherContract
    QPI::PreManagementRightsTransfer_output pre_output;

    // Call PRE_ACQUIRE_SHARES in other contract after transferring invocationReward
    __qpiCallSystemProcOfOtherContract<PRE_ACQUIRE_SHARES>(contractIndex, pre_input, pre_output, invocationReward);

    if (pre_output.ok)
    {
        // TODO: transfer

        // TODO: init input
        QPI::PostManagementRightsTransfer_input post_input;
        // Output is unused, but needed for generalized interface
        QPI::NoData post_output;

        // Call POST_ACQUIRE_SHARES in other contract without transferring an invocationReward
        __qpiCallSystemProcOfOtherContract<POST_ACQUIRE_SHARES>(contractIndex, post_input, post_output, 0);
    }

    return pre_output.ok;
}


bool QPI::QpiContextProcedureCall::distributeDividends(long long amountPerShare) const
{
    if (amountPerShare < 0 || amountPerShare * NUMBER_OF_COMPUTORS > MAX_AMOUNT)
    {
        return false;
    }

    const int index = spectrumIndex(_currentContractId);

    if (index < 0)
    {
        return false;
    }

    const long long remainingAmount = energy(index) - amountPerShare * NUMBER_OF_COMPUTORS;

    if (remainingAmount < 0)
    {
        return false;
    }

    if (decreaseEnergy(index, amountPerShare * NUMBER_OF_COMPUTORS))
    {
        ACQUIRE(universeLock);

        for (int issuanceIndex = 0; issuanceIndex < ASSETS_CAPACITY; issuanceIndex++)
        {
            if (((*((unsigned long long*)assets[issuanceIndex].varStruct.issuance.name)) & 0xFFFFFFFFFFFFFF) == *((unsigned long long*)contractDescriptions[_currentContractIndex].assetName)
                && assets[issuanceIndex].varStruct.issuance.type == ISSUANCE
                && isZero(assets[issuanceIndex].varStruct.issuance.publicKey))
            {
                // TODO: use list to iterate through owners
                long long shareholderCounter = 0;
                for (int ownershipIndex = 0; shareholderCounter < NUMBER_OF_COMPUTORS && ownershipIndex < ASSETS_CAPACITY; ownershipIndex++)
                {
                    if (assets[ownershipIndex].varStruct.ownership.issuanceIndex == issuanceIndex
                        && assets[ownershipIndex].varStruct.ownership.type == OWNERSHIP)
                    {
                        long long possessorCounter = 0;

                        // TODO: use list to iterate through possessors
                        for (int possessionIndex = 0; possessorCounter < assets[ownershipIndex].varStruct.ownership.numberOfShares && possessionIndex < ASSETS_CAPACITY; possessionIndex++)
                        {
                            if (assets[possessionIndex].varStruct.possession.ownershipIndex == ownershipIndex
                                && assets[possessionIndex].varStruct.possession.type == POSSESSION)
                            {
                                possessorCounter += assets[possessionIndex].varStruct.possession.numberOfShares;

                                increaseEnergy(assets[possessionIndex].varStruct.possession.publicKey, amountPerShare * assets[possessionIndex].varStruct.possession.numberOfShares);

                                if (!contractActionTracker.addQuTransfer(_currentContractId, assets[possessionIndex].varStruct.possession.publicKey, amountPerShare * assets[possessionIndex].varStruct.possession.numberOfShares))
                                    __qpiAbort(ContractErrorTooManyActions);

                                const QuTransfer quTransfer = { _currentContractId , assets[possessionIndex].varStruct.possession.publicKey , amountPerShare * assets[possessionIndex].varStruct.possession.numberOfShares };
                                logger.logQuTransfer(quTransfer);
                            }
                        }

                        shareholderCounter += possessorCounter;
                    }
                }

                break;
            }
        }

        RELEASE(universeLock);
    }

    return true;
}


long long QPI::QpiContextProcedureCall::issueAsset(unsigned long long name, const QPI::id& issuer, signed char numberOfDecimalPlaces, long long numberOfShares, unsigned long long unitOfMeasurement) const
{
    if (((unsigned char)name) < 'A' || ((unsigned char)name) > 'Z'
        || name > 0xFFFFFFFFFFFFFF)
    {
        return 0;
    }
    for (unsigned int i = 1; i < 7; i++)
    {
        if (!((unsigned char)(name >> (i * 8))))
        {
            while (++i < 7)
            {
                if ((unsigned char)(name >> (i * 8)))
                {
                    return 0;
                }
            }

            break;
        }
    }
    for (unsigned int i = 1; i < 7; i++)
    {
        if (!((unsigned char)(name >> (i * 8)))
            || (((unsigned char)(name >> (i * 8))) >= '0' && ((unsigned char)(name >> (i * 8))) <= '9')
            || (((unsigned char)(name >> (i * 8))) >= 'A' && ((unsigned char)(name >> (i * 8))) <= 'Z'))
        {
            // Do nothing
        }
        else
        {
            return 0;
        }
    }

    // Any time an asset is issued via QPI either invocator or contract can be the issuer. Zero is prohibited in this case.
    if (isZero(issuer) || (issuer != _currentContractId && issuer != _invocator))
    {
        return 0;
    }

    if (numberOfShares <= 0 || numberOfShares > MAX_AMOUNT)
    {
        return 0;
    }

    if (unitOfMeasurement > 0xFFFFFFFFFFFFFF)
    {
        return 0;
    }

    char nameBuffer[7] = { char(name), char(name >> 8), char(name >> 16), char(name >> 24), char(name >> 32), char(name >> 40), char(name >> 48) };
    char unitOfMeasurementBuffer[7] = { char(unitOfMeasurement), char(unitOfMeasurement >> 8), char(unitOfMeasurement >> 16), char(unitOfMeasurement >> 24), char(unitOfMeasurement >> 32), char(unitOfMeasurement >> 40), char(unitOfMeasurement >> 48) };
    int issuanceIndex, ownershipIndex, possessionIndex;
    numberOfShares = ::issueAsset(issuer, nameBuffer, numberOfDecimalPlaces, unitOfMeasurementBuffer, numberOfShares, _currentContractIndex, &issuanceIndex, &ownershipIndex, &possessionIndex);

    return numberOfShares;
}


// TODO: remove after testing period, because numberOfShares() can do this and more
long long QPI::QpiContextFunctionCall::numberOfPossessedShares(unsigned long long assetName, const m256i& issuer, const m256i& owner, const m256i& possessor, unsigned short ownershipManagingContractIndex, unsigned short possessionManagingContractIndex) const
{
    return ::numberOfPossessedShares(assetName, issuer, owner, possessor, ownershipManagingContractIndex, possessionManagingContractIndex);
}

sint64 QPI::QpiContextFunctionCall::numberOfShares(const QPI::AssetIssuanceId& issuanceId, const QPI::AssetOwnershipSelect& ownership, const QPI::AssetPossessionSelect& possession) const
{
    return ::numberOfShares(issuanceId, ownership, possession);
}

bool QPI::QpiContextProcedureCall::releaseShares(uint64 assetName, const id& issuer, const id& owner, const id& possessor, sint64 numberOfShares, uint16 destinationOwnershipManagingContractIndex, uint16 destinationPossessionManagingContractIndex) const
{
    // TODO

    return false;
}

long long QPI::QpiContextProcedureCall::transferShareOwnershipAndPossession(unsigned long long assetName, const m256i& issuer, const m256i& owner, const m256i& possessor, long long numberOfShares, const m256i& newOwnerAndPossessor) const
{
    if (numberOfShares <= 0 || numberOfShares > MAX_AMOUNT)
    {
        return -((long long)(MAX_AMOUNT + 1));
    }

    ACQUIRE(universeLock);

    int issuanceIndex = issuer.m256i_u32[0] & (ASSETS_CAPACITY - 1);
iteration:
    if (assets[issuanceIndex].varStruct.issuance.type == EMPTY)
    {
        RELEASE(universeLock);

        return -numberOfShares;
    }
    else
    {
        if (assets[issuanceIndex].varStruct.issuance.type == ISSUANCE
            && ((*((unsigned long long*)assets[issuanceIndex].varStruct.issuance.name)) & 0xFFFFFFFFFFFFFF) == assetName
            && assets[issuanceIndex].varStruct.issuance.publicKey == issuer)
        {
            int ownershipIndex = owner.m256i_u32[0] & (ASSETS_CAPACITY - 1);
        iteration2:
            if (assets[ownershipIndex].varStruct.ownership.type == EMPTY)
            {
                RELEASE(universeLock);

                return -numberOfShares;
            }
            else
            {
                if (assets[ownershipIndex].varStruct.ownership.type == OWNERSHIP
                    && assets[ownershipIndex].varStruct.ownership.issuanceIndex == issuanceIndex
                    && assets[ownershipIndex].varStruct.ownership.publicKey == owner
                    && assets[ownershipIndex].varStruct.ownership.managingContractIndex == _currentContractIndex) // TODO: This condition needs extra attention during refactoring!
                {
                    int possessionIndex = possessor.m256i_u32[0] & (ASSETS_CAPACITY - 1);
                iteration3:
                    if (assets[possessionIndex].varStruct.possession.type == EMPTY)
                    {
                        RELEASE(universeLock);

                        return -numberOfShares;
                    }
                    else
                    {
                        if (assets[possessionIndex].varStruct.possession.type == POSSESSION
                            && assets[possessionIndex].varStruct.possession.ownershipIndex == ownershipIndex
                            && assets[possessionIndex].varStruct.possession.publicKey == possessor)
                        {
                            if (assets[possessionIndex].varStruct.possession.managingContractIndex == _currentContractIndex) // TODO: This condition needs extra attention during refactoring!
                            {
                                if (assets[possessionIndex].varStruct.possession.numberOfShares >= numberOfShares)
                                {
                                    int destinationOwnershipIndex, destinationPossessionIndex;
                                    ::transferShareOwnershipAndPossession(ownershipIndex, possessionIndex, newOwnerAndPossessor, numberOfShares, &destinationOwnershipIndex, &destinationPossessionIndex, false);

                                    RELEASE(universeLock);

                                    return assets[possessionIndex].varStruct.possession.numberOfShares;
                                }
                                else
                                {
                                    RELEASE(universeLock);

                                    return assets[possessionIndex].varStruct.possession.numberOfShares - numberOfShares;
                                }
                            }
                            else
                            {
                                RELEASE(universeLock);

                                return -numberOfShares;
                            }
                        }
                        else
                        {
                            possessionIndex = (possessionIndex + 1) & (ASSETS_CAPACITY - 1);

                            goto iteration3;
                        }
                    }
                }
                else
                {
                    ownershipIndex = (ownershipIndex + 1) & (ASSETS_CAPACITY - 1);

                    goto iteration2;
                }
            }
        }
        else
        {
            issuanceIndex = (issuanceIndex + 1) & (ASSETS_CAPACITY - 1);

            goto iteration;
        }
    }
}

bool QPI::QpiContextFunctionCall::isAssetIssued(const m256i& issuer, unsigned long long assetName) const
{
    bool res = ::issuanceIndex(issuer, assetName) != NO_ASSET_INDEX;
    return res;
}
