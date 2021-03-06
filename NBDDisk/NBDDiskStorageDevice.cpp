//
//  NBDDiskStorageDevice.cpp
//  NBDDisk
//
//  Created by Steve O'Brien on 11/13/12.
//  Copyright (c) 2012 Steve O'Brien. All rights reserved.
//

#include <IOKit/storage/IOBlockStorageDevice.h>
#include <sys/types.h>                       // (miscfs/devfs/devfs.h, ...)
#include <miscfs/devfs/devfs.h>              // (devfs_make_node, ...)
#include <sys/buf.h>                         // (buf_t, ...)
#include <sys/fcntl.h>                       // (FWRITE, ...)
#include <sys/ioccom.h>                      // (IOCGROUP, ...)
#include <sys/proc.h>                        // (proc_is64bit, ...)
#include <sys/stat.h>                        // (S_ISBLK, ...)
#include <sys/systm.h>                       // (DEV_BSIZE, ...)
#include <IOKit/assert.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/storage/IOMedia.h>
#include "NBDDiskStorageDevice.h"
#include "NBDBlockService.h"

#define super IOBlockStorageDevice


OSDefineMetaClassAndStructors(cc_obrien_NBDDiskStorageDevice, super)


bool cc_obrien_NBDDiskStorageDevice::init(OSDictionary *properties)
{
	if(! super::init(properties))
	{
		return false;
	}
	
	this->setProperty(kIOBSDNameKey, "nbd");
	this->setProperty(kIOBSDMajorKey, 92);
	
	return true;
}


bool cc_obrien_NBDDiskStorageDevice::attach(IOService *provider)
{
	if(! super::attach(provider))
	{
		return false;
	}
	
	this->provider = OSDynamicCast(cc_obrien_NBDBlockService, provider);
	if(! this->provider)
	{
		return false;
	}

	if(this->provider->getByteCount() % this->provider->getBlockSize())
	{
		// not divisible?!
		return false;
	}
	
	this->blockCount = this->provider->getByteCount() / this->provider->getBlockSize();
	this->lastAskedState = this->provider->isReady();
	
	return true;
}


void cc_obrien_NBDDiskStorageDevice::detach(IOService *provider)
{
	if(provider == this->provider)
	{
		this->provider = NULL;
	}
	
	super::detach(provider);
}


IOReturn cc_obrien_NBDDiskStorageDevice::doEjectMedia()
{
	this->provider->doEjectMedia();
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::doFormatMedia(UInt64 byteCapacity)
{
	return kIOReturnUnsupported;
}


UInt32 cc_obrien_NBDDiskStorageDevice::doGetFormatCapacities(UInt64 *byteCapacities, UInt32 capacitiesMaxCount) const
{
	if(! byteCapacities)
	{
		return 1;  // we require an array with a size of only 1
	}
	
	if(capacitiesMaxCount < 1)
	{
		return 0;  // error
	}

	// populate one item
	byteCapacities[0] = this->provider->getByteCount();
	return 1;
}


IOReturn cc_obrien_NBDDiskStorageDevice::doLockUnlockMedia(bool doLock)
{
	if(doLock)
		return kIOReturnUnsupported;
	
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::doSynchronizeCache()
{
	// TODO
	return kIOReturnSuccess;
}


char * cc_obrien_NBDDiskStorageDevice::getVendorString()
{
	return (char *) "(networked)";
}


char * cc_obrien_NBDDiskStorageDevice::getProductString()
{
	return (char *) "NBD Disk";
}


char * cc_obrien_NBDDiskStorageDevice::getRevisionString()
{
	return (char *) "1";
}


char * cc_obrien_NBDDiskStorageDevice::getAdditionalDeviceInfoString()
{
	return (char *) "nbd device host=%s port=%d size=%lld bytes";
}


IOReturn cc_obrien_NBDDiskStorageDevice::reportBlockSize(UInt64 *blockSize)
{
	*blockSize = (UInt64) this->provider->getBlockSize();
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::reportEjectability(bool *isEjectable)
{
	*isEjectable = true;
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::reportLockability(bool *isLockable)
{
	*isLockable = false;
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::reportMaxValidBlock(UInt64 *maxBlock)
{
	*maxBlock = this->blockCount - 1;
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::reportMediaState(bool *mediaPresent, bool *changedState)
{
	const bool ready = (this->provider && this->provider->isReady());
	*mediaPresent = ready;
	*changedState = (this->lastAskedState != ready);
	this->lastAskedState = ready;
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::reportPollRequirements(bool *pollRequired, bool *pollIsExpensive)
{
	*pollRequired = true;
	*pollIsExpensive = false;
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::reportRemovability(bool *isRemovable)
{
	*isRemovable = true;
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::reportWriteProtection(bool *isWriteProtected)
{
	*isWriteProtected = ! (this->provider && this->provider->isWritable());
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::getWriteCacheState(bool *enabled)
{
	*enabled = false;
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::setWriteCacheState(bool enabled)
{
	if(enabled)
	{
		return kIOReturnUnsupported;
	}
	
	return kIOReturnSuccess;
}


IOReturn cc_obrien_NBDDiskStorageDevice::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageAttributes *attributes, IOStorageCompletion *completion)
{
	IOByteCount actualCount = 0;
	
	cc_obrien_NBDBlockService *provider = this->provider;
	
	if(! (provider && this->provider->isReady()) )
	{
		return kIOReturnNotAttached;
	}
	
	if( (block + nblks) > (this->blockCount) )
	{
		return kIOReturnBadArgument;
	}

	const UInt32 blockSize = provider->getBlockSize();
	
	if(buffer->getDirection() == kIODirectionIn)
	{
		actualCount = provider->performRead(
			buffer,
			block * blockSize,
			nblks * blockSize);
	}
	else if(buffer->getDirection() == kIODirectionOut)
	{
		if(! provider->isWritable())
		{
			return kIOReturnNotWritable;
		}

		actualCount = provider->performWrite(
			buffer,
			block * blockSize,
			nblks * blockSize);
	}
	else
	{
		return kIOReturnBadArgument;
	}
	
	(completion->action)(completion->target, completion->parameter, kIOReturnSuccess, actualCount);
	return kIOReturnSuccess;
}
