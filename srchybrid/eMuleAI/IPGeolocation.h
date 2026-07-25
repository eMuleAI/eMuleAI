//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include <atlcoll.h>
#include "resource.h"
#include "Preferences.h"
#include "eMuleAI/Address.h"

#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <system_error>
#include <type_traits>
#include "maxminddb.h"

namespace IPGeolocationDB
{
	/// Errors defined in maxminddb.h, converted to C++ enums.
	enum class MMDBStatus
	{
		success						= MMDB_SUCCESS								,	///<= 0
		file_open					= MMDB_FILE_OPEN_ERROR						,	///<= 1
		corrupt_search_tree			= MMDB_CORRUPT_SEARCH_TREE_ERROR			,	///<= 2
		invalid_metadata			= MMDB_INVALID_METADATA_ERROR				,	///<= 3
		io							= MMDB_IO_ERROR								,	///<= 4
		out_of_memory				= MMDB_OUT_OF_MEMORY_ERROR					,	///<= 5
		unknown_db_format			= MMDB_UNKNOWN_DATABASE_FORMAT_ERROR		,	///<= 6
		invalid_data				= MMDB_INVALID_DATA_ERROR					,	///<= 7
		invalid_lookup_path			= MMDB_INVALID_LOOKUP_PATH_ERROR			,	///<= 8
		lookup_path_does_not_match	= MMDB_LOOKUP_PATH_DOES_NOT_MATCH_DATA_ERROR,	///<= 9
		invalid_node_number			= MMDB_INVALID_NODE_NUMBER_ERROR			,	///<= 10
		ipv6_lookup_in_ipv4_db		= MMDB_IPV6_LOOKUP_IN_IPV4_DATABASE_ERROR		///<= 11
	};


	/** Error class used to return libmaxminddb error codes and messages when C++ exceptions are thrown.
	 * @see @p MMDB_strerror()
	 */
	class ErrorCategory : public std::error_category
	{
		public:

			/// Returns a unique name.
			virtual const char *name( void ) const noexcept;

			/// Convert an MMDB error/status code into a readable text string message.
			virtual std::string message( int code ) const;
	};

	/// All error category objects should actually be references to the exact same object, which is provided by this function.
	const ErrorCategory &get_error_category( void ) noexcept;

	/// Associate an error value with the category.
	std::error_code make_error_code( MMDBStatus s );

	/// Associate an error_condition with the category.
	std::error_condition make_error_condition( MMDBStatus s );
}

namespace IPGeolocationDB
{
	/// Map of std::string to std::string.
	typedef std::map<std::string, std::string> MStr;

	/// Vector of C-style string pointers.
	typedef std::vector<const char *> VCStr;

	/** The DB class wraps an IP geolocation MMDB database and retrieves information based on IP addresses.
	 */
	class DB final
	{
		public:

			/// Destructor.
			~DB( void );

			/** Constructor.
			* @param [in] database_filename The @p .mmdb database file path.  The file name can be relative to the current
			* working directory, or an absolute path and filename.
			*
			* ~~~~
			* std::string filename = "/opt/my_project_files/dbip-city-lite.mmdb";
			*
			* IPGeolocationDB::DB db( filename );
			*
			* std::cout << db.get_lib_version_mmdb() << std::endl;
			* std::cout << db.get_metadata()         << std::endl;
			* ~~~~
			*
			* @warning <b>An object of type @p IPGeolocationDB::DB cannot be constructed without a valid database file!</b>
			* 
			* @note The MMDB database must be available before this class can be constructed.
			*/
			DB( const std::string &database_filename = IPGEOLOCATION_DB_FILENAMEA );

			/** Get the MMDB library version number.
			 *
			 * ~~~~
			 * IPGeolocationDB::DB db;
			 * std::cout << db.get_lib_version_mmdb() << std::endl;
			 * ~~~~
			 *
			 * Example output:
			 * ~~~~{.txt}
			 * 1.2.0
			 * ~~~~
			 */
			std::string get_lib_version_mmdb( void ) const;

			/** Get the IP geolocation helper version number.
			 *
			 * ~~~~
			 * IPGeolocationDB::DB db;
			 * std::cout << db.get_lib_version_ipgeolocation() << std::endl;
			 * ~~~~
			 *
			 * Example output:
			 * ~~~~{.txt}
			 * 0.0.1-1992
			 * ~~~~
			 */
			std::string get_lib_version_ipgeolocation( void ) const;

			/** Get the database metadata.  This returns a raw @p MMDB_metadata_s structure.  This call is intended
			 * mostly for internal purposes, or to call directly into the original C MMDB API.
			 * @see @ref get_metadata()
			 */
			MMDB_metadata_s get_metadata_raw( void );

			/** Get the database metadata as a JSON string.
			 *
			 * ~~~~
			 * IPGeolocationDB::DB db;
			 * std::cout << db.get_metadata() << std::endl;
			 * ~~~~
			 *
			 * Example output:
			 * ~~~~{.json}
			 * {
			 *     "binary_format_major_version" : 2,
			 *     "binary_format_minor_version" : 0,
			 *     "build_epoch" : 1475588619,
			 *     "database_type" : "dbip-city-lite",
			 *     "description" : { "en" : "IP to City Lite database" },
			 *     "ip_version" : 6,
			 *     "languages" : [ "de", "en", "es", "fr", "ja", "pt-BR", "ru", "zh-CN" ],
			 *     "node_count" : 4065439,
			 *     "record_size" : 28
			 * }
			 * ~~~~
			 * @see @ref get_metadata_raw()
			 */
			std::string get_metadata( void );

			/** Look up an IP address.  This returns a raw @p MMDB_lookup_result_s structure.  This call is intended
			 * mostly for internal purposes, or to call directly into the original C MMDB API.
			 * @see @ref lookup()
			 */
			MMDB_lookup_result_s lookup_raw( const std::string &ip_address );

			/** Look up an IP address and return a JSON string of everything found.
			 *
			 * ~~~~
			 * IPGeolocationDB::DB db;
			 * std::string json = db.lookup( "65.44.217.6" );
			 * std::cout << json << std::endl;
			 * ~~~~
			 *
			 * Example output:
			 * ~~~~{.json}
			 * {  "city"      : { "names" : { "en" : "Fresno" } },
			 *    "continent" : { "code" : "NA", "names" : { "en" : "North America" } },
			 *    "country"   : { "iso_code" : "US", "names" : { "en" : "United States" } },
			 *    "location"  : { "accuracy_radius" : 200,
			 *                    "latitude" : 36.6055,
			 *                    "longitude" : -119.752,
			 *                    "time_zone" : "America/Los_Angeles" },
			 *    "postal"    : { "code" : "93725" },
			 *    "subdivisions" : [ { "iso_code" : "CA", "names" : { "en" : "California" } } ]
			 * }
			 * ~~~~
			 * @note <i>For clarity, some of the JSON entries have been removed in this example output.</i>
			 */
			std::string lookup( const std::string &ip_address );

			/** Return a @p std::map of many of the key fields available when looking up an address.  This includes
			 * fields such as subdivision name, city name, country name, continent name, longitude, latitude, accuracy
			 * radius, and relevant iso codes.  Not all fields are available for all addresses and languages.
			 *
			 * @note The method is mis-named. It returns @b many fields, but definitely does not return @b all fields.
			 * To see all fields, see @ref lookup().
			 *
			 * ~~~~
			 * IPGeolocationDB::DB db;
			 * IPGeolocationDB::MStr m = db.get_all_fields( "65.44.217.6" );
			 * for ( const auto iter : m )
			 * {
			 * 		std::cout << iter.first << " -> " << iter.second << std::endl;
			 * }
			 * ~~~~
			 *
			 * Example output:
			 * ~~~~{.txt}
			 * accuracy_radius -> 200
			 * city -> Fresno
			 * continent -> North America
			 * country -> United States
			 * country_iso_code -> US
			 * latitude -> 36.605500
			 * longitude -> -119.752200
			 * postal_code -> 93725
			 * query_ip_address -> 65.44.217.6
			 * query_language -> en
			 * registered_country -> United States
			 * subdivision -> California
			 * subdivision_iso_code -> CA
			 * time_zone -> America/Los_Angeles
			 * ~~~~
			 */
			MStr get_all_fields( const std::string &ip_address, const std::string &language = "en" );

			/** Get a specific field, or an empty string if the field does not exist.
			 *
			 * A lookup of the given IP address needs to be performed every time this is called, which makes this less
			 * efficient than the other @p get_field() method which takes a @p MMDB_lookup_result_s parameter.
			 *
			 * ~~~~
			 * IPGeolocationDB::DB db;
			 * std::string city     = db.get_field( "65.44.217.6", "en", IPGeolocationDB::VCStr { "city"    , "names"    } );
			 * std::string country  = db.get_field( "65.44.217.6", "en", IPGeolocationDB::VCStr { "country" , "names"    } );
			 * std::string latitude = db.get_field( "65.44.217.6", "en", IPGeolocationDB::VCStr { "location", "latitude" } );
			 * ~~~~
			 */
			std::string get_field( const std::string &ip_address, const std::string &language, const VCStr &v );

			/** Get a specific field, or an empty string if the field does not exist.
			 *
			 * This is a more efficient call than the other @p get_field() method since the address doesn't need to
			 * be looked up in the database at every call.
			 *
			 * ~~~~
			 * IPGeolocationDB::DB db;
			 * MMDB_lookup_result_s result = db.lookup_raw( "65.44.217.6" );
			 * std::string city     = db.get_field( &result, "en", IPGeolocationDB::VCStr { "city"    , "names"    } );
			 * std::string country  = db.get_field( &result, "en", IPGeolocationDB::VCStr { "country" , "names"    } );
			 * std::string latitude = db.get_field( &result, "en", IPGeolocationDB::VCStr { "location", "latitude" } );
			 * ~~~~
			 */
			std::string get_field( MMDB_lookup_result_s *lookup, const std::string &language, const VCStr &v );

			/** Process the specified node list and return a JSON-format string.
			 *
			 * @warning This @b will de-allocate the node list prior to returning; do not call
			 * @p MMDB_free_entry_data_list() on the node.
			 *
			 * This call is intended mostly for internal purposes.  @see @ref lookup()  @see @ref get_metadata()
			 */
			std::string to_json( MMDB_entry_data_list_s *node );

		private:

			/// Internal handle to the database.  @see @p MMDB_open()
			MMDB_s mmdb;

			/// Traverse a list of nodes and build up a JSON string.  This method is not exposed.  It is for internal use only.
			void create_json_from_entry( std::stringstream &ss, size_t depth, uint32_t data_size, MMDB_entry_data_list_s * &next, const bool in_array = false );

			/// Look up a specific name (e.g., "city") and store it in the specified map.  This method is not exposed.  It is for internal use only.
			void add_to_map( IPGeolocationDB::MStr &m, MMDB_lookup_result_s *node, const std::string &name, const std::string &language, const VCStr &v );
	};
}


namespace std
{
	/// Make sure that MMDBStatus enums can be converted to error codes.
	template <>
	struct is_error_code_enum<IPGeolocationDB::MMDBStatus> : public true_type {};
}


#define NO_FLAG 0
#define FLAG_WIDTH 20
#define FLAG_HEIGHT 14 

struct GeolocationData_Struct {
	CString			Country;
	CString			CountryCode;
	CString			City;
	WORD			FlagIndex;
};

typedef CTypedPtrArray<CPtrArray, GeolocationData_Struct*> CIPGeolocationArray;

enum IPGeolocationMode {
	IPGEO_DISABLE = 0,
	IPGEO_COUNTRYCODE,
	IPGEO_COUNTRY,
	IPGEO_COUNTRYCITY
};

const uint16 CountryCodeFlagArraySize = 254;

struct CountryCodeFlag_Struct
{
	CString	strCountryCode;
	uint16	uResourceID;
};

class CIPGeolocation
{
	public:
		CIPGeolocation(void);
		~CIPGeolocation(void);
		
		void	LoadIPGeolocation(bool bAddToStatusBar = false);
		void	UnloadIPGeolocation();
		void	Reset(); // Reset IP geolocation data held by items.
		void	Redraw(); // Redraw windows holding IP geolocation data.
		void	LoadFlags();
		void	UnloadFlags();
		bool	EnsureFlagsLoaded();
		int		GetFlagIndexByCountryCode(const CString& strCountryCode) const;
		static CString GetLocalizedCountryName(const CString& strCountryCode, const CString& strFallbackName = CString());
		static CString FormatLocalizedCountryNameAndCode(const CString& strCountryCode, const CString& strFallbackName = CString());
		const bool	IsIPGeolocationActive() { return IPGeolocationLoaded && thePrefs.GetIPGeolocationMode() != IPGEO_DISABLE; }
		const bool	ShowCountryFlag();
		const GeolocationData_Struct QueryGeolocationData(const CAddress& IP);
		const CString GetGeolocationData(const GeolocationData_Struct m_structServerCountry, bool bForceCountryCity =false) const;
		CImageList* GetFlagImageList() { return &FlagImageList; }
		IMAGELISTDRAWPARAMS GetFlagImageDrawParams(CDC* dc,int iIndex,POINT point) const;

		static LPCTSTR GetDefaultUpdateURLTemplate();
		static CString ExpandUpdateURLTemplate(const CString& strURLTemplate);
		static bool UpdateIPGeolocationFromURL(const CString& url, bool bInteractive = true);
		static bool IsIPGeolocationDownloadActive();
		static bool GetIPGeolocationDownloadOverlayInfo(CString& strTitle, CString& strDetail, UINT& uDone, UINT& uTotal);
		static void FinishIPGeolocationDownloadOverlayDelay();
		static void CancelIPGeolocationDownload();
		static LRESULT OnIPGeolocationDownloadProgress(LPARAM lParam);
		static LRESULT OnIPGeolocationDownloadFinished(LPARAM lParam);

		CString m_strIPGeolocationCityFile;
		IPGeolocationDB::DB* db;

	private:
		bool	m_bRunning;	//check is program current running, if it's under init or shutdown, set to false
		CImageList	FlagImageList;
		bool	IPGeolocationLoaded;
		CRBMap<CString, uint16>	CountryCodeFlagMap;
		const CString GetFilePath();

		const CountryCodeFlag_Struct CountryCodeFlagArray[CountryCodeFlagArraySize] = {
			{ L"N/A",IDI_FLAG_NOFLAG },
			{ L"A1", IDI_FLAG_A1 },
			{ L"A2", IDI_FLAG_A2 },
			{ L"AD", IDI_FLAG_AD },
			{ L"AE", IDI_FLAG_AE },
			{ L"AF", IDI_FLAG_AF },
			{ L"AG", IDI_FLAG_AG },
			{ L"AI", IDI_FLAG_AI },
			{ L"AL", IDI_FLAG_AL },
			{ L"AM", IDI_FLAG_AM },
			{ L"AO", IDI_FLAG_AO },
			{ L"AQ", IDI_FLAG_AQ },
			{ L"AR", IDI_FLAG_AR },
			{ L"AS", IDI_FLAG_AS },
			{ L"AT", IDI_FLAG_AT },
			{ L"AU", IDI_FLAG_AU },
			{ L"AW", IDI_FLAG_AW },
			{ L"AX", IDI_FLAG_AX },
			{ L"AZ", IDI_FLAG_AZ },
			{ L"BA", IDI_FLAG_BA },
			{ L"BB", IDI_FLAG_BB },
			{ L"BD", IDI_FLAG_BD },
			{ L"BE", IDI_FLAG_BE },
			{ L"BF", IDI_FLAG_BF },
			{ L"BG", IDI_FLAG_BG },
			{ L"BH", IDI_FLAG_BH },
			{ L"BI", IDI_FLAG_BI },
			{ L"BJ", IDI_FLAG_BJ },
			{ L"BL", IDI_FLAG_BL },
			{ L"BM", IDI_FLAG_BM },
			{ L"BN", IDI_FLAG_BN },
			{ L"BO", IDI_FLAG_BO },
			{ L"BQ", IDI_FLAG_BQ },
			{ L"BR", IDI_FLAG_BR },
			{ L"BS", IDI_FLAG_BS },
			{ L"BT", IDI_FLAG_BT },
			{ L"BV", IDI_FLAG_BV },
			{ L"BW", IDI_FLAG_BW },
			{ L"BY", IDI_FLAG_BY },
			{ L"BZ", IDI_FLAG_BZ },
			{ L"CA", IDI_FLAG_CA },
			{ L"CC", IDI_FLAG_CC },
			{ L"CD", IDI_FLAG_CD },
			{ L"CF", IDI_FLAG_CF },
			{ L"CG", IDI_FLAG_CG },
			{ L"CH", IDI_FLAG_CH },
			{ L"CI", IDI_FLAG_CI },
			{ L"CK", IDI_FLAG_CK },
			{ L"CL", IDI_FLAG_CL },
			{ L"CM", IDI_FLAG_CM },
			{ L"CN", IDI_FLAG_CN },
			{ L"CO", IDI_FLAG_CO },
			{ L"CR", IDI_FLAG_CR },
			{ L"CU", IDI_FLAG_CU },
			{ L"CV", IDI_FLAG_CV },
			{ L"CW", IDI_FLAG_CW },
			{ L"CX", IDI_FLAG_CX },
			{ L"CY", IDI_FLAG_CY },
			{ L"CZ", IDI_FLAG_CZ },
			{ L"DE", IDI_FLAG_DE },
			{ L"DJ", IDI_FLAG_DJ },
			{ L"DK", IDI_FLAG_DK },
			{ L"DM", IDI_FLAG_DM },
			{ L"DO", IDI_FLAG_DO },
			{ L"DZ", IDI_FLAG_DZ },
			{ L"EC", IDI_FLAG_EC },
			{ L"EE", IDI_FLAG_EE },
			{ L"EG", IDI_FLAG_EG },
			{ L"EH", IDI_FLAG_EH },
			{ L"ER", IDI_FLAG_ER },
			{ L"ES", IDI_FLAG_ES },
			{ L"ET", IDI_FLAG_ET },
			{ L"EU", IDI_FLAG_EU },
			{ L"FI", IDI_FLAG_FI },
			{ L"FJ", IDI_FLAG_FJ },
			{ L"FK", IDI_FLAG_FK },
			{ L"FM", IDI_FLAG_FM },
			{ L"FO", IDI_FLAG_FO },
			{ L"FR", IDI_FLAG_FR },
			{ L"GA", IDI_FLAG_GA },
			{ L"GB", IDI_FLAG_GB },
			{ L"GD", IDI_FLAG_GD },
			{ L"GE", IDI_FLAG_GE },
			{ L"GF", IDI_FLAG_GF },
			{ L"GG", IDI_FLAG_GG },
			{ L"GH", IDI_FLAG_GH },
			{ L"GI", IDI_FLAG_GI },
			{ L"GL", IDI_FLAG_GL },
			{ L"GM", IDI_FLAG_GM },
			{ L"GN", IDI_FLAG_GN },
			{ L"GP", IDI_FLAG_GP },
			{ L"GQ", IDI_FLAG_GQ },
			{ L"GR", IDI_FLAG_GR },
			{ L"GS", IDI_FLAG_GS },
			{ L"GT", IDI_FLAG_GT },
			{ L"GU", IDI_FLAG_GU },
			{ L"GW", IDI_FLAG_GW },
			{ L"GY", IDI_FLAG_GY },
			{ L"HK", IDI_FLAG_HK },
			{ L"HM", IDI_FLAG_HM },
			{ L"HN", IDI_FLAG_HN },
			{ L"HR", IDI_FLAG_HR },
			{ L"HT", IDI_FLAG_HT },
			{ L"HU", IDI_FLAG_HU },
			{ L"ID", IDI_FLAG_ID },
			{ L"IE", IDI_FLAG_IE },
			{ L"IL", IDI_FLAG_IL },
			{ L"IM", IDI_FLAG_IM },
			{ L"IN", IDI_FLAG_IN },
			{ L"IO", IDI_FLAG_IO },
			{ L"IQ", IDI_FLAG_IQ },
			{ L"IR", IDI_FLAG_IR },
			{ L"IS", IDI_FLAG_IS },
			{ L"IT", IDI_FLAG_IT },
			{ L"JE", IDI_FLAG_JE },
			{ L"JM", IDI_FLAG_JM },
			{ L"JO", IDI_FLAG_JO },
			{ L"JP", IDI_FLAG_JP },
			{ L"KE", IDI_FLAG_KE },
			{ L"KG", IDI_FLAG_KG },
			{ L"KH", IDI_FLAG_KH },
			{ L"KI", IDI_FLAG_KI },
			{ L"KM", IDI_FLAG_KM },
			{ L"KN", IDI_FLAG_KN },
			{ L"KP", IDI_FLAG_KP },
			{ L"KR", IDI_FLAG_KR },
			{ L"KW", IDI_FLAG_KW },
			{ L"KY", IDI_FLAG_KY },
			{ L"KZ", IDI_FLAG_KZ },
			{ L"LA", IDI_FLAG_LA },
			{ L"LB", IDI_FLAG_LB },
			{ L"LC", IDI_FLAG_LC },
			{ L"LI", IDI_FLAG_LI },
			{ L"LK", IDI_FLAG_LK },
			{ L"LR", IDI_FLAG_LR },
			{ L"LS", IDI_FLAG_LS },
			{ L"LT", IDI_FLAG_LT },
			{ L"LU", IDI_FLAG_LU },
			{ L"LV", IDI_FLAG_LV },
			{ L"LY", IDI_FLAG_LY },
			{ L"MA", IDI_FLAG_MA },
			{ L"MC", IDI_FLAG_MC },
			{ L"MD", IDI_FLAG_MD },
			{ L"ME", IDI_FLAG_ME },
			{ L"MF", IDI_FLAG_MF },
			{ L"MG", IDI_FLAG_MG },
			{ L"MH", IDI_FLAG_MH },
			{ L"MK", IDI_FLAG_MK },
			{ L"ML", IDI_FLAG_ML },
			{ L"MM", IDI_FLAG_MM },
			{ L"MN", IDI_FLAG_MN },
			{ L"MO", IDI_FLAG_MO },
			{ L"MP", IDI_FLAG_MP },
			{ L"MQ", IDI_FLAG_MQ },
			{ L"MR", IDI_FLAG_MR },
			{ L"MS", IDI_FLAG_MS },
			{ L"MT", IDI_FLAG_MT },
			{ L"MU", IDI_FLAG_MU },
			{ L"MV", IDI_FLAG_MV },
			{ L"MW", IDI_FLAG_MW },
			{ L"MX", IDI_FLAG_MX },
			{ L"MY", IDI_FLAG_MY },
			{ L"MZ", IDI_FLAG_MZ },
			{ L"NA", IDI_FLAG_NA },
			{ L"NC", IDI_FLAG_NC },
			{ L"NE", IDI_FLAG_NE },
			{ L"NF", IDI_FLAG_NF },
			{ L"NG", IDI_FLAG_NG },
			{ L"NI", IDI_FLAG_NI },
			{ L"NL", IDI_FLAG_NL },
			{ L"NO", IDI_FLAG_NO },
			{ L"NP", IDI_FLAG_NP },
			{ L"NR", IDI_FLAG_NR },
			{ L"NU", IDI_FLAG_NU },
			{ L"NZ", IDI_FLAG_NZ },
			{ L"OM", IDI_FLAG_OM },
			{ L"PA", IDI_FLAG_PA },
			{ L"PE", IDI_FLAG_PE },
			{ L"PF", IDI_FLAG_PF },
			{ L"PG", IDI_FLAG_PG },
			{ L"PH", IDI_FLAG_PH },
			{ L"PK", IDI_FLAG_PK },
			{ L"PL", IDI_FLAG_PL },
			{ L"PM", IDI_FLAG_PM },
			{ L"PN", IDI_FLAG_PN },
			{ L"PR", IDI_FLAG_PR },
			{ L"PS", IDI_FLAG_PS },
			{ L"PT", IDI_FLAG_PT },
			{ L"PW", IDI_FLAG_PW },
			{ L"PY", IDI_FLAG_PY },
			{ L"QA", IDI_FLAG_QA },
			{ L"RE", IDI_FLAG_RE },
			{ L"RO", IDI_FLAG_RO },
			{ L"RS", IDI_FLAG_RS },
			{ L"RU", IDI_FLAG_RU },
			{ L"RW", IDI_FLAG_RW },
			{ L"SA", IDI_FLAG_SA },
			{ L"SB", IDI_FLAG_SB },
			{ L"SC", IDI_FLAG_SC },
			{ L"SD", IDI_FLAG_SD },
			{ L"SE", IDI_FLAG_SE },
			{ L"SG", IDI_FLAG_SG },
			{ L"SH", IDI_FLAG_SH },
			{ L"SI", IDI_FLAG_SI },
			{ L"SJ", IDI_FLAG_SJ },
			{ L"SK", IDI_FLAG_SK },
			{ L"SL", IDI_FLAG_SL },
			{ L"SM", IDI_FLAG_SM },
			{ L"SN", IDI_FLAG_SN },
			{ L"SO", IDI_FLAG_SO },
			{ L"SR", IDI_FLAG_SR },
			{ L"SS", IDI_FLAG_SS },
			{ L"ST", IDI_FLAG_ST },
			{ L"SV", IDI_FLAG_SV },
			{ L"SX", IDI_FLAG_SX },
			{ L"SY", IDI_FLAG_SY },
			{ L"SZ", IDI_FLAG_SZ },
			{ L"TC", IDI_FLAG_TC },
			{ L"TD", IDI_FLAG_TD },
			{ L"TF", IDI_FLAG_TF },
			{ L"TG", IDI_FLAG_TG },
			{ L"TH", IDI_FLAG_TH },
			{ L"TJ", IDI_FLAG_TJ },
			{ L"TK", IDI_FLAG_TK },
			{ L"TL", IDI_FLAG_TL },
			{ L"TM", IDI_FLAG_TM },
			{ L"TN", IDI_FLAG_TN },
			{ L"TO", IDI_FLAG_TO },
			{ L"TR", IDI_FLAG_TR },
			{ L"TT", IDI_FLAG_TT },
			{ L"TV", IDI_FLAG_TV },
			{ L"TW", IDI_FLAG_TW },
			{ L"TZ", IDI_FLAG_TZ },
			{ L"UA", IDI_FLAG_UA },
			{ L"UG", IDI_FLAG_UG },
			{ L"UM", IDI_FLAG_UM },
			{ L"US", IDI_FLAG_US },
			{ L"UY", IDI_FLAG_UY },
			{ L"UZ", IDI_FLAG_UZ },
			{ L"VA", IDI_FLAG_VA },
			{ L"VC", IDI_FLAG_VC },
			{ L"VE", IDI_FLAG_VE },
			{ L"VG", IDI_FLAG_VG },
			{ L"VI", IDI_FLAG_VI },
			{ L"VN", IDI_FLAG_VN },
			{ L"VU", IDI_FLAG_VU },
			{ L"WF", IDI_FLAG_WF },
			{ L"WS", IDI_FLAG_WS },
			{ L"XK", IDI_FLAG_XK },
			{ L"YE", IDI_FLAG_YE },
			{ L"YT", IDI_FLAG_YT },
			{ L"ZA", IDI_FLAG_ZA },
			{ L"ZM", IDI_FLAG_ZM },
			{ L"ZW", IDI_FLAG_ZW }
		};
};