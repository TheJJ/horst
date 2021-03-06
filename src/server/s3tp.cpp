#include <s3tp/core/Logger.h>
#include <sstream>

#include "../event/req_shell_command.h"
#include "../horst.h"
#include "../satellite.h"
#include "s3tp.h"

namespace horst {

	S3TPServer::S3TPServer(int port, std::string socketpath)
		: S3tpCallback(),
		expected{0},
		process{nullptr} {

		s3tpSocketPath = strdup(socketpath.c_str());

		// Create channel instance and set default config
		this->s3tp_cfg.port = port; // default Local port to bind to
		this->s3tp_cfg.options = 0;
		this->s3tp_cfg.channel = 3; // This represents the virtual channel used by NanoLink
	}

	S3TPServer::~S3TPServer() {
		uv_poll_stop(&this->connection);
	}

	void S3TPServer::on_s3tp_event(uv_poll_t *handle, int, int events) {
		auto s3tp_link_ref = (S3TPServer*) handle->data;

		if (s3tp_link_ref->channel != nullptr && events & UV_READABLE) {
			s3tp_link_ref->channel->handleIncomingData();
		}
		if (s3tp_link_ref->channel != nullptr && events & UV_WRITABLE) {
			s3tp_link_ref->channel->handleOutgoingData();
		}

		if (s3tp_link_ref->channel != nullptr)
			s3tp_link_ref->update_events();
	}

	void S3TPServer::update_events() {
		int next_events = 0;
		int current_events = this->channel->getActiveEvents();
		if (current_events & S3TP_ASYNC_EVENT_READ)
			next_events |= UV_READABLE;
		if (current_events & S3TP_ASYNC_EVENT_WRITE)
			next_events |= UV_WRITABLE;

		uv_poll_start(&this->connection,
		              next_events | UV_DISCONNECT,
		              &S3TPServer::on_s3tp_event);
	}

	bool S3TPServer::reconnect() {
		int r = 0;
		int error = 0;
		int s3tp_fd;

		// Are we connected already?
		if (this->channel != NULL) {
			return true;
		}

		// Cancel any running command
		if (this->process) {
			LOG_INFO("Killing process....");
			this->process->kill();

			// Wait for kill
			uv_timer_start(
				&this->timer,
				[] (uv_timer_t *handle) {
					auto srv = (S3TPServer*) handle->data;
					srv->process = nullptr;
					srv->reconnect();
				},
				1 * 100, // time in milliseconds, just wait for the next libuv event loop cycle
				0         // don't repeat
			);
			return false;
		}

		// Reset internal buffer state
		this->buf.clear();
		this->expected = 0;
		this->outbuf.clear();

		// Try to connect
		LOG_INFO("[s3tp] Try to reconnect...");
		this->channel = std::make_unique<S3tpChannelEvent>(this->s3tp_cfg, *this);

		// Bind channel to S3TP daemon
		this->channel->bind(error);
		if (error == 0) {
			r = this->channel->accept();
			if (r < 0) {
				LOG_WARN("[s3tp] Failed to register for s3tp events: " + std::string(strerror(-r)));
				this->channel = NULL;
			}
		} else {
			LOG_WARN("[s3tp] Failed to bind to s3tp: " + std::to_string(error));
			this->channel = NULL;
		}

		if (this->channel == NULL) {
			// Regularly check connection and reconnect
			uv_timer_start(
				&this->timer,
				[] (uv_timer_t *handle) {
					// yes, handle is not a poll_t, but
					// we just care for its -> data member anyway.
					auto srv = (S3TPServer*) handle->data;
					srv->reconnect();
				},
				5 * 1000, // time in milliseconds, try to reconnect every second
				0         // don't repeat
			);

			LOG_WARN("[s3tp] Reconnect failed...");
			return false;
		} else {
			s3tp_fd = this->channel->getSocket()->getFileDescriptor();

			// initialize the s3tp fd events polling object
			uv_poll_init(this->loop, &this->connection, s3tp_fd);

			// make `this` reachable in event loop callbacks.
			this->connection.data = this;
			update_events();

			LOG_INFO("[s3tp] Reconnect succeeded...");
			return true;
		}
	}

	bool S3TPServer::start(uv_loop_t *loop_ref) {
		this->loop = loop_ref;

		uv_timer_init(this->loop, &this->timer);
		this->timer.data = this;

		return reconnect();
	}

	void S3TPServer::onConnected(S3tpChannel&) {
		LOG_INFO("[s3tp] Connection is active");
	}

	void S3TPServer::onDisconnected(S3tpChannel&, int error) {
		LOG_WARN("[s3tp] S3TP disconnected (" + std::to_string(error) + ")");
		uv_poll_stop(&this->connection);
		this->channel = NULL;
		this->reconnect();
	}

	bool S3TPServer::compare_to_buf(const std::string& str)
	{
		if (this->buf.size() < sizeof(this->expected))
			return false;

		return std::equal(buf.begin() + sizeof(this->expected), buf.end(), str.begin(), str.end());
	}

	void S3TPServer::onDataReceived(S3tpChannel&, char *data, size_t len) {
		MessageType type;;
		const size_t headersize = sizeof(this->expected) + sizeof(type);
		LOG_INFO("[s3tp] Received " + std::to_string(len) + " bytes");

		// Copy data into buffer
		this->buf.insert(this->buf.end(), data, data + len);

		// Not enough data yet
		if (this->buf.size() < headersize) {
			LOG_DEBUG("[s3tp] Not enough data received, waiting for more...");
			return;
		}

		// Receive header
		if (this->expected == 0) {
			LOG_DEBUG("[s3tp] Receiving new command...");
			std::memcpy(&this->expected, this->buf.data(), sizeof(this->expected));
			std::memcpy(&type, this->buf.data() + sizeof(this->expected), sizeof(type));
			if (this->expected == 0) {
				LOG_WARN("[s3tp] Invalid length received, closing...");
				this->close();
				return;
			}
		}

		// Receive data
		if (this->buf.size() >= this->expected + headersize) {
			if (this->process) {
				if (type == MessageType::ENDOFFILE) {
					LOG_INFO("[s3tp] Received EOF");
					this->process->close_input();
				} else {
					LOG_INFO("[s3tp] Receiving input data...");
					this->process->input(this->buf.data()+headersize, this->expected);
				}
			} else {
				LOG_INFO("[s3tp] Receiving command data...");
				std::string command(this->buf.begin()+headersize, this->buf.begin()+headersize+this->expected);
				process = std::make_unique<Process>(this->loop, command, true, [this] (Process* process, long exit_code) {

					LOG_INFO("[s3tp] process caboomed...");
					// Return exit code
					std::stringstream ss;
					ss << "[exit] " << exit_code << std::endl;
					this->send(ss.str().c_str(), ss.str().size(), MessageType::ENDOFFILE);

					this->process = nullptr;

					if (this->outbuf.size() > 0) {
						// Send all remaining data, connection will be closed afterwards
						this->send_buf();
					} else {
						// perform soft-close, so that s3tp will still send out remaining buffered data
						(void) this->channel->disconnect();
					}
				});
				if (process.get() != nullptr) {
					// Immediately send back that the command was received.
					this->send("", 0, MessageType::STARTED);
					this->process->start_output();
				} else {
					LOG_WARN("[s3tp] Error while creating request, closing...");
					this->close();
					return;
				}
			}

			this->buf.erase(this->buf.begin(), this->buf.begin() + headersize + this->expected);
			this->expected = 0;
		}
	}

	void S3TPServer::send(const char* msg, uint32_t len, MessageType type) {
		if (len == 0 && type == MessageType::NONE)
			return;
		LOG_INFO("[s3tp] Queueing " + std::to_string(len) + " bytes of data for sending");
		if (!this->channel) {
			LOG_WARN("[s3tp] Tried to send without open connection!");
			return;
		}

		// Put length of data into first 4 bytes, message type into byte 5
		size_t oldlength = this->outbuf.size();
		this->outbuf.resize(oldlength + sizeof(len) + sizeof(type));
		std::memcpy(this->outbuf.data() + oldlength, &len, sizeof(len));
		std::memcpy(this->outbuf.data() + oldlength + sizeof(len), &type, sizeof(type));

		// Append actual data
		this->outbuf.insert(this->outbuf.end(), msg, msg + len);

		// Try to send all data
		this->send_buf();

		update_events();
	}

	bool S3TPServer::send_buf() {
		if (this->outbuf.size() == 0)
		    return true;

		LOG_INFO("[s3tp] Sending " + std::to_string(this->outbuf.size()) + " bytes of data");
		int r = this->channel->send(this->outbuf.data(), this->outbuf.size());
		if (r == ERROR_BUFFER_FULL) {
			LOG_WARN("[s3tp] Buffer is full!");
			if (this->process)
				this->process->stop_output();
			return false;
		}
		if (r < 0) {
			LOG_WARN("[s3tp] Failed to send data!");
			this->close();
			return false;
		}
		this->outbuf.clear();

		// Close connection, if process exited
		if (this->process == nullptr) {
			// perform soft-close, so that s3tp will still send out remaining buffered data
			(void) this->channel->disconnect();
		}

		return true;
	}

	void S3TPServer::close() {
		LOG_DEBUG("[s3tp] Connection closed internally");
		uv_poll_stop(&this->connection);
		this->channel = NULL;
		this->reconnect();
	}

	void S3TPServer::onBufferFull(S3tpChannel&) {
		LOG_INFO("[s3tp] Buffer is full");
		if (this->process)
			this->process->stop_output();
	}

	void S3TPServer::onBufferEmpty(S3tpChannel&) {
		LOG_INFO("[s3tp] Buffer is empty");
		if (this->process)
			this->process->start_output();
		this->send_buf();
	}

	void S3TPServer::onError(int error) {
		LOG_WARN("[s3tp] connection closed with error " + std::to_string(error));
	}

} // horst
