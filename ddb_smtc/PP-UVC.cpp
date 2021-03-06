// PP-UWP-Interop.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"


#include <windows.media.h>
#include <wrl/event.h>
#include "PP-UVC.h"
#include <assert.h>
#include <ShCore.h>
#include <Shlwapi.h>

class EH_t {
public:
	void operator<<(HRESULT hr) {
		(void) hr;
#ifdef _DEBUG
		// on an if() so you can breakpoint it conveniently
		if ( FAILED( hr ) ) {
			assert( !"fail" );
		}
#endif
	}
};

static EH_t EH;

using namespace ABI::Windows::Media;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Media::Playback;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

static void DebugExamine(IInspectable * i) {
	HRESULT hr;
	ULONG count; IID * iids;
	hr = i->GetIids(&count, &iids);

	HSTRING blah;
	hr = i->GetRuntimeClassName(&blah);

	UINT32 len;
	auto ptr = WindowsGetStringRawBuffer(blah, &len);

	// Breakpoint here to see properties of the passed object
	return;
}

static ComPtr<IMediaPlayer> getMediaPlayer() {
	ComPtr<IMediaPlayer> player;
	HRESULT hr;
	
	hr = ActivateInstance(HStringReference(RuntimeClass_Windows_Media_Playback_MediaPlayer).Get(), &player );
	EH << hr;
	return player;

#if 0
	// obsolete code path
	ComPtr<IBackgroundMediaPlayerStatics> bmpStatics;
	hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Media_Playback_BackgroundMediaPlayer).Get(), &bmpStatics);
	EH << hr;
	if ( FAILED( hr ) ) return nullptr;

	EH << bmpStatics->get_Current(&player);
#endif
	return player;
}

static ComPtr<ISystemMediaTransportControls> getSMTCProper() {

	// This is how things are SUPPOSED to work but they don't
	// "invalid window handle" happens if we do that because no CoreWindow exists for our app

	HRESULT hr; ComPtr<ISystemMediaTransportControls> ret;
	ComPtr<ISystemMediaTransportControlsStatics> statics;
	hr = GetActivationFactory(HStringReference(RuntimeClass_Windows_Media_SystemMediaTransportControls).Get(), &statics);
	EH << hr;
	if (FAILED(hr)) return nullptr;

	hr = statics->GetForCurrentView(&ret);
	// EH << hr;
	if (FAILED(hr)) return nullptr;

	return ret;

}

static ComPtr<ISystemMediaTransportControls> getSMTC() {

#if 0
	// Would be too easy if this just worked sanely wouldn't it
	EH << ActivateInstance(HStringReference(RuntimeClass_Windows_Media_SystemMediaTransportControls).Get(), &ret);
	return ret;
#endif

	ComPtr<ISystemMediaTransportControls> ret;

	// Try the proper API (that's not known to work at the time I am writing this)
	ret = getSMTCProper();
	if ( ret ) return ret; // wishful mode

	
	// PROBLEM: at this point - obtaining or creating a MediaPlayer - we spawn a separate unwanted session in Windows Mixer.
	// The session doesn't go away with the release of IMediaPlayer
	ComPtr<IMediaPlayer> player = getMediaPlayer();
	if ( ! player ) return nullptr;
	ComPtr<IMediaPlayer2> player2;

	EH << player.As( &player2 );
	EH << player2->get_SystemMediaTransportControls( & ret );


	// Detach from SMTC instance
	// Doesn't help with unwanted mixer entry
	ComPtr<IMediaPlayer3> player3;
	if (SUCCEEDED( player.As( & player3 ) ) ) {
		ComPtr<IMediaPlaybackCommandManager> cmdmanager;
		if ( SUCCEEDED( player3->get_CommandManager(&cmdmanager) ) ) {
			EH << cmdmanager->put_IsEnabled( false );
		}
		
	}
	
	// Close the player object
	// Doesn't help with unwanted mixer entry
	ComPtr<IClosable> stfu;
	if (SUCCEEDED(player.As(&stfu))) {
		EH << stfu->Close();
	}

	
	return ret;

}

namespace PP {
	namespace UVC {

		class APIImpl : public API {
			ComPtr<ISystemMediaTransportControls> g_controls;
			UserEventCallback * g_callback = nullptr;
			void HandleUserEvent(ISystemMediaTransportControlsButtonPressedEventArgs * args) {
				SystemMediaTransportControlsButton value;
				if (SUCCEEDED(args->get_Button(&value))) {
					switch (value) {
					case SystemMediaTransportControlsButton_Play:
						g_callback->Play();
						break;
					case SystemMediaTransportControlsButton_Pause:
						g_callback->Pause();
						break;
					case SystemMediaTransportControlsButton_Stop:
						g_callback->Stop();
						break;
					case SystemMediaTransportControlsButton_Next:
						g_callback->Next();
						break;
					case SystemMediaTransportControlsButton_Previous:
						g_callback->Previous();
						break;
					case SystemMediaTransportControlsButton_FastForward:
						g_callback->FastForward();
						break;
					case SystemMediaTransportControlsButton_Rewind:
						g_callback->Rewind();
						break;
					}
				}
			}

		public:
			bool Setup(UserEventCallback * cb) {

				/*EH << */RoInitialize(RO_INIT_MULTITHREADED);
					
				g_controls = getSMTC();
				if (!g_controls) return false;
					
				g_callback = cb;

				EH << g_controls->put_IsEnabled(true);

				EventRegistrationToken tokenButtonPressed;

				typedef ABI::Windows::Foundation::ITypedEventHandler<SystemMediaTransportControls*, SystemMediaTransportControlsButtonPressedEventArgs*> IButtonPressedHandler;
				auto bpHandler = Callback<IButtonPressedHandler>([this](ISystemMediaTransportControls * sender, ISystemMediaTransportControlsButtonPressedEventArgs * args) { try { HandleUserEvent(args); } catch (...) {} return S_OK; });
				EH << g_controls->add_ButtonPressed(bpHandler.Get(), &tokenButtonPressed);

#if 0
				EventRegistrationToken tokenPropertyChanged, tokenSeek, tokenRepeat;
				typedef ABI::Windows::Foundation::ITypedEventHandler<SystemMediaTransportControls*, SystemMediaTransportControlsPropertyChangedEventArgs*> IPropertyChangedHandler;
				auto propHandler = Callback<IPropertyChangedHandler>([](ISystemMediaTransportControls*, ISystemMediaTransportControlsPropertyChangedEventArgs * args) {
					// FIX ME
					return S_OK;
				});
				EH << g_controls->add_PropertyChanged(propHandler.Get(), &tokenPropertyChanged);

				ComPtr<ISystemMediaTransportControls2> controls2;
				if (SUCCEEDED(g_controls.As(&controls2))) {
					typedef ABI::Windows::Foundation::ITypedEventHandler<SystemMediaTransportControls*, PlaybackPositionChangeRequestedEventArgs *> IPositionChangeReuqestHandler;
					auto seekHandler = Callback<IPositionChangeReuqestHandler>([](ISystemMediaTransportControls*, IPlaybackPositionChangeRequestedEventArgs * args) {
						// FIX ME
						return S_OK;
					});
					EH << controls2->add_PlaybackPositionChangeRequested(seekHandler.Get(), &tokenSeek);

					typedef ABI::Windows::Foundation::ITypedEventHandler<SystemMediaTransportControls*, AutoRepeatModeChangeRequestedEventArgs *> IAutoRepeatModeRequestHandler;
					auto repeatHandler = Callback<IAutoRepeatModeRequestHandler>([](ISystemMediaTransportControls *, IAutoRepeatModeChangeRequestedEventArgs * args) {
						// FIX ME
						return S_OK;
					});
					EH << controls2->add_AutoRepeatModeChangeRequested(repeatHandler.Get(), &tokenRepeat);
				}
#endif


				EH << g_controls->put_IsPauseEnabled(true);
				EH << g_controls->put_IsPlayEnabled(true);
				EH << g_controls->put_IsNextEnabled(true);
				EH << g_controls->put_IsPreviousEnabled(true);
				EH << g_controls->put_IsStopEnabled(true);
				EH << g_controls->put_PlaybackStatus(MediaPlaybackStatus_Closed);

				return true;
			}

			void Stopped() {
				EH << g_controls->put_PlaybackStatus(MediaPlaybackStatus_Stopped);
			}
			void NewTrack(const TrackInfo & info) {
				ComPtr<ISystemMediaTransportControlsDisplayUpdater> updater;
				EH << g_controls->get_DisplayUpdater(&updater);
				EH << updater->put_Type(MediaPlaybackType_Music);
				ComPtr<IMusicDisplayProperties> props;
				ComPtr<IMusicDisplayProperties2> props2;
				ComPtr<IMusicDisplayProperties3> props3;
				EH << updater->get_MusicProperties(&props);
				if (info.title != nullptr) {
					EH << props->put_Title(HStringReference(info.title).Get());
				}
				if (info.artist != nullptr) {
					EH << props->put_Artist(HStringReference(info.artist).Get());
				}
				if (info.albumArtist != nullptr) {
					EH << props->put_AlbumArtist(HStringReference(info.albumArtist).Get());
				}
				if (SUCCEEDED(props.As(&props2))) {
					if (info.albumTitle != nullptr) {
						EH << props2->put_AlbumTitle(HStringReference(info.albumTitle).Get());
					}
					if (info.trackNumber != 0) {
						EH << props2->put_TrackNumber(info.trackNumber);
					}
				}
				if (SUCCEEDED(props.As(&props3))) {
					if (info.trackCount != 0) {
						EH << props3->put_AlbumTrackCount(info.trackCount);
					}
				}

				bool bHaveCover = false;
				if (info.imgBytes > 0 && info.imgBytes == (UINT) /* downcast sanity if 64bit */ info.imgBytes ) {
					ComPtr<IStream> memStream;
					memStream.Attach( SHCreateMemStream((const BYTE*)info.imgData, (UINT) info.imgBytes ) );
					if ( memStream ) {
						using namespace ABI::Windows::Storage::Streams;
						ComPtr<IRandomAccessStream> stream;
						EH << CreateRandomAccessStreamOverStream(memStream.Get(), BSOS_DEFAULT, __uuidof(IRandomAccessStream), (void**)stream.ReleaseAndGetAddressOf());
						ComPtr<IRandomAccessStreamReferenceStatics> api;
						EH << GetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_Streams_RandomAccessStreamReference).Get(), &api);
						ComPtr<IRandomAccessStreamReference> ref;
						EH << api->CreateFromStream(stream.Get(), ref.ReleaseAndGetAddressOf());
						EH << updater->put_Thumbnail(ref.Get());
						bHaveCover = true;
					}
				}
				// Must explicitly clear the thumbnail or else the previous one sticks
				if (! bHaveCover ) EH << updater->put_Thumbnail( nullptr );

				EH << updater->Update();
			}
			void Paused(bool bPaused) {
				EH << g_controls->put_PlaybackStatus(bPaused ? MediaPlaybackStatus_Paused : MediaPlaybackStatus_Playing);
			}
		};
	}
}

extern "C" {
	PP::UVC::API * PP_UVC_Init(PP::UVC::UserEventCallback * cb) {
		using namespace PP::UVC;
		APIImpl * obj = nullptr;
		try {
			obj = new APIImpl;
			if (obj->Setup(cb)) return obj;
		} catch(...) {}
		delete obj;
		return nullptr;
	}
}