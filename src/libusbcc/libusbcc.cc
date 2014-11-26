/* vim:ts=4
 *
 * Copyleft 2014…2015  Michał Gawron
 * Marduk Unix Labs, http://mulabs.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Visit http://www.gnu.org/licenses/gpl-3.0.html for more information on licensing.
 */

// Lib:
#include <exception>
#include <iostream>//XXX
#include <libusb.h>

// Local:
#include "libusbcc.h"


namespace libusb {

StatusException::StatusException (libusb_error code):
	Exception (libusb_strerror (code))
{ }


ControlTransfer::ControlTransfer (uint8_t request, uint16_t value, uint16_t index):
	request (request),
	value (value),
	index (index)
{ }


Device::Device (libusb_device* device, libusb_device_handle* handle):
	_device (device),
	_handle (handle)
{
	libusb_ref_device (device);
}


Device::~Device()
{
	cleanup();
}


Device::Device (Device&& other):
	_device (other._device),
	_handle (other._handle)
{
	other.reset();
}


Device&
Device::operator= (Device&& other)
{
	cleanup();
	_handle = other._handle;
	_device = other._device;
	other.reset();
	return *this;
}


USBVersion
Device::usb_version() const
{
	return static_cast<USBVersion> (descriptor().bcdUSB);
}


const char*
Device::usb_version_str() const
{
	switch (usb_version())
	{
		case USBVersion::V_1_1:
			return "1.1";
		case USBVersion::V_2_0:
			return "2.0";
		case USBVersion::V_3_0:
			return "3.0";
		default:
			return "unknown";
	}
}


uint16_t
Device::release_version() const
{
	return descriptor().bcdDevice;
}


std::string
Device::release_version_str() const
{
	uint16_t v = release_version();
	return std::to_string ((v >> 8) & 0xff) + '.' + std::to_string (v & 0xff);
}


VendorID
Device::vendor_id() const
{
	return descriptor().idVendor;
}


ProductID
Device::product_id() const
{
	return descriptor().idProduct;
}


DeviceClass
Device::usb_class() const
{
	return static_cast<DeviceClass> (descriptor().bDeviceClass);
}


DeviceSubClass
Device::usb_sub_class() const
{
	return static_cast<DeviceSubClass> (descriptor().bDeviceSubClass);
}


DeviceProtocol
Device::usb_protocol() const
{
	return static_cast<DeviceProtocol> (descriptor().bDeviceProtocol);
}


std::string
Device::manufacturer() const
{
	return get_usb_string (descriptor().iManufacturer);
}


std::string
Device::product() const
{
	return get_usb_string (descriptor().iProduct);
}


std::string
Device::serial_number() const
{
	return get_usb_string (descriptor().iSerialNumber);
}


uint8_t
Device::num_configurations() const
{
	return descriptor().bNumConfigurations;
}


uint8_t
Device::max_packet_size_0() const
{
	return descriptor().bMaxPacketSize0;
}


void
Device::send (ControlTransfer const& ct, int timeout_ms, std::vector<uint8_t> const& buffer)
{
	// For to-device transfers we can assume that buffer will not change.
	// Therefore allow const_cast to make C function happy.
	uint8_t* ll_buffer = const_cast<uint8_t*> (buffer.data());
	int bytes_transferred = libusb_control_transfer (_handle, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT,
													 ct.request, ct.value, ct.index, ll_buffer, buffer.size(), timeout_ms);
	if (Session::is_error (bytes_transferred))
		throw StatusException (static_cast<libusb_error> (bytes_transferred));
}


std::vector<uint8_t>
Device::receive (ControlTransfer const& ct, int timeout_ms)
{
	// Control transfers may carry up to 64 bytes of data, so allocate a vector of 64 bytes:
	std::vector<uint8_t> buffer (64, 0);
	int bytes_transferred = libusb_control_transfer (_handle, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN,
													 ct.request, ct.value, ct.index, buffer.data(), buffer.size(), timeout_ms);
	if (Session::is_error (bytes_transferred))
		throw StatusException (static_cast<libusb_error> (bytes_transferred));
	buffer.resize (bytes_transferred);
	return buffer;
}


inline void
Device::reset()
{
	_handle = nullptr;
	_device = nullptr;
}


inline void
Device::cleanup()
{
	if (_handle)
		libusb_close (_handle);
	if (_device)
		libusb_unref_device (_device);
}


inline libusb_device_descriptor&
Device::descriptor() const
{
	if (_descriptor)
		return *_descriptor;
	else
	{
		_descriptor.emplace (libusb_device_descriptor());
		int err = libusb_get_device_descriptor (_device, _descriptor.get_ptr());
		if (Session::is_error (err))
			throw StatusException (static_cast<libusb_error> (err));
		return *_descriptor;
	}
}


std::string
Device::get_usb_string (int string_id) const
{
	// TODO what about UTF-16
	if (string_id > 0)
	{
		char buffer[256];
		int chars = libusb_get_string_descriptor_ascii (_handle, string_id, reinterpret_cast<unsigned char*> (buffer), sizeof (buffer));
		if (chars < 0)
			throw StatusException (static_cast<libusb_error> (chars));
		buffer[chars] = 0;
		return std::string (buffer);
	}
	else
		return std::string();
}


DeviceDescriptor::DeviceDescriptor (libusb_device* device):
	_device (device)
{
	libusb_ref_device (_device);
}


DeviceDescriptor::DeviceDescriptor (DeviceDescriptor const& other):
	_device (other._device)
{
	libusb_ref_device (_device);
}


DeviceDescriptor::DeviceDescriptor (DeviceDescriptor&& other):
	_device (other._device)
{
	other.reset();
}


DeviceDescriptor::~DeviceDescriptor()
{
	cleanup();
}


DeviceDescriptor&
DeviceDescriptor::operator= (DeviceDescriptor const& other)
{
	cleanup();
	_device = other._device;
	libusb_ref_device (_device);
	return *this;
}


DeviceDescriptor&
DeviceDescriptor::operator= (DeviceDescriptor&& other)
{
	cleanup();
	_device = other._device;
	other.reset();
	return *this;
}


Device
DeviceDescriptor::open() const
{
	libusb_device_handle* handle;
	int err = libusb_open (_device, &handle);
	if (Session::is_error (err))
		throw StatusException (static_cast<libusb_error> (err));
	return Device (_device, handle);
}


inline void
DeviceDescriptor::reset()
{
	_device = nullptr;
}


inline void
DeviceDescriptor::cleanup()
{
	if (_device)
		libusb_unref_device (_device);
}


Session::Session()
{
	try {
		int err = libusb_init (&_context);
		if (Session::is_error (err))
			throw StatusException (static_cast<libusb_error> (err));
	}
	catch (...)
	{
		std::throw_with_nested (Exception ("failed to create libusb session"));
	}
}


Session::~Session()
{
	libusb_exit (_context);
}


DeviceDescriptors&
Session::device_descriptors()
{
	try {
		if (_device_descriptors.empty())
		{
			libusb_device** device_list;
			std::size_t num_devices = libusb_get_device_list (_context, &device_list);

			if (is_error (num_devices))
			{
				num_devices = 0;
				throw StatusException (static_cast<libusb_error> (num_devices));
			}

			// Build list of Devices:
			for (size_t i = 0; i < num_devices; ++i)
				_device_descriptors.emplace_back (device_list[i]);

			libusb_free_device_list (device_list, 1);
		}
	}
	catch (...)
	{
		std::throw_with_nested (Exception ("failed to get device list"));
	}

	return _device_descriptors;
}


bool
Session::is_error (int status)
{
	switch (status)
	{
		case LIBUSB_SUCCESS:
			return false;

		case LIBUSB_ERROR_IO:
		case LIBUSB_ERROR_INVALID_PARAM:
		case LIBUSB_ERROR_ACCESS:
		case LIBUSB_ERROR_NO_DEVICE:
		case LIBUSB_ERROR_NOT_FOUND:
		case LIBUSB_ERROR_BUSY:
		case LIBUSB_ERROR_TIMEOUT:
		case LIBUSB_ERROR_OVERFLOW:
		case LIBUSB_ERROR_PIPE:
		case LIBUSB_ERROR_INTERRUPTED:
		case LIBUSB_ERROR_NO_MEM:
		case LIBUSB_ERROR_NOT_SUPPORTED:
		case LIBUSB_ERROR_OTHER:
			return true;

		default:
			return false;
	}
}

} // namespace libusb
